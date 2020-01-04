
# Flex Backend Configuration

**The Flex Backend is experimental. Everything in here is subject to change.**

The "Flex" backend is configured through a Lua file which

* defines the structure of the output tables and
* defines functions to map the OSM data to the data format to be used
  in the database.

This way you have a lot of control over how the data should look like in the
database.

## Lua config file

All configuration is done through the `osm2pgsql` object in Lua. It has the
following fields:

* `osm2pgsql.version`: The version of osm2pgsql as a string.
* `osm2pgsql.srid`: The SRID set on the command line (with `-l|--latlong`,
  `-m|--merc`, or `-E|--proj`).
* `osm2pgsql.mode`: Either `"create"` or `"append"` depending on the command
  line options (`--create` or `-a|--append`).
* `osm2pgsql.stage`: Either `1` or `2` (1st/2nd stage processing the data).
  See below.
* `osm2pgsql.userdata`: To store your user data. See below.

The following functions are defined:

* `osm2pgsql.define_node_table(name, columns)`: Define a node table.
* `osm2pgsql.define_way_table(name, columns)`: Define a way table.
* `osm2pgsql.define_relation_table(name, columns)`: Define a relation table.
* `osm2pgsql.define_area_table(name, columns)`: Define an area table.
* `osm2pgsql.define_table()`: Define a table. This is the more flexible
  function behind all the other `define_*_table()` functions. It gives you
  more control than the more convenient other functions.
* `osm2pgsql.mark(type, id)`: Mark the OSM object of the specified type (`"w"`
  or `"r"`) with the specified id. The OSM object will trigger a call to the
  processing function again in stage 2.
* `osm2pgsql.get_bbox()`: Get the bounding box of the current node or way. Only
  works inside the `osm2pgsql.process_node()` and `osm2pgsql.process_way()`
  functions.

You are expected to define one or more of the following functions:

* `osm2pgsql.process_node()`: Called for each node.
* `osm2pgsql.process_way()`: Called for each way.
* `osm2pgsql.process_relation()`: Called for each relation.

### Defining a table

You have to define one or more tables where your data should end up. This
is done with the `osm2pgsql.define_table()` function or one of the slightly
more convenient functions `osm2pgsql.define_(node|way|relation|area)_table()`.

Each table is either a *node table*, *way table*, *relation table*, or *area
table*. This means that the data for that table comes primarily from a node,
way, relation, or area respectively. Osm2pgsql makes sure that the OSM object
id will be stored in the table so that later updates to those OSM objects (or
deletions) will be properly reflected in the tables. Area tables are special,
they can contain data derived from ways or from relations. Way ids will be
stored as is, relation ids will be stored as negative numbers. (You can define
tables that don't have any ids, but those tables will never be updated by
osm2pgsql.) You can also define tables that take *any OSM object*, but only
with the `osm2pgsql.define_table()` function.

If you are using the `osm2pgsql.define_(node|way|relation|area)_table()`
convenience functions, osm2pgsql will automatically create an id column named
`(node|way|relation|area)_id`, respectively. If you want more control over
the id column(s), use the `osm2pgsql.define_table()` function.

Most tables will have a geometry column. (Currently only zero or one geometry
columns are supported.) The types of the geometry column possible depend on
the type of the input data. For node tables you are pretty much restricted
to point geometries, but there is a variety of options for relation tables
for instance.

The supported geometry types are:
* `geometry`: Any kind of geometry. Also used for area tables that should hold
  both polygon and multipolygon geometries.
* `point`: Point geometry, usually created from nodes.
* `linestring`: Linestring geometry, usually created from ways.
* `polygon`: Polygon geometry for area tables, created from ways or relations.
* `multipoint`: Currently not used.
* `multilinestring`: Created from (possibly split up) ways or relations.
* `multipolygon`: For area tables, created from ways or relations.

The only thing you have to do here is to define the geometry type you want and
osm2pgsql will create the right geometry for you from the OSM data and fill it
in.

A column of type `area` will be filled automatically with the area of the
geometry. This will only work for (multi)polygons.

In addition to id and geometry columns, each table can have any number of
"normal" columns using any type supported by PostgreSQL. Some types are
specially recognized by osm2pgsql:

* `text`: A text string.
* `boolean`: Interprets string values `"true"`, `"yes"` as `true` and all
   others as `false`. Boolean and integer values will also work in the usual
   way.
* `int2`, `smallint`: 16bit signed integer. Values too large to fit will be
  truncated in some unspecified way.
* `int4`, `int`, `integer`: 32bit signed integer. Values too large to fit will be
  truncated in some unspecified way.
* `int8`, `bigint`: 64bit signed integer. Values too large to fit will be
  truncated in some unspecified way.
* `real`: A real number.
* `hstore`: Automatically filled from a Lua table with only strings as keys
  and values.
* `json` and `jsonb`: Not supported yet.
* `direction`: Interprets values `"true"`, `"yes"`, and `"1"` as 1, `"-1"` as
  `-1`, and everything else as `0`. Useful for `oneway` tags etc.

Instead of the above types you can use any SQL type you want. If you do that
you have to supply the PostgreSQL string representation for that type when
adding data to such columns (or Lua nil to set the column to `NULL`).

### Processing callbacks

You are expected to define one or more of the following functions:

* `osm2pgsql.process_node(object)`: Called for each node.
* `osm2pgsql.process_way(object)`: Called for each way.
* `osm2pgsql.process_relation(object)`: Called for each relation.

They all have a single argument of type table (here called `object`) and no
return values.

The parameter table (`object`) has the following fields:

* `id`: The id of the node, way, or relation.
* `tags`: A table with all the tags of the object.
* `version`, `timestamp`, `changeset`, `uid`, and `user`: Attributes of the
  OSM object. These are only available if the `-x|--extra-attributes` option
  is used and the OSM input file actually contains those fields. The
  `timestamp` contains the time in seconds since the epoch (midnight
  1970-01-01).

Ways have the following additional fields:
* `is_closed`: A boolean telling you whether the way geometry is closed, ie
  the first and last node are the same.
* `nodes`: An array with the way node ids.

Relations have the following additional field:
* `members`: An array with member tables. Each member table has the fields
  `type` (values `n`, `w`, or `r`), `ref` (member id) and `role`.

You can do anything in those processing functions to decide what to do with
this data. If you are not interested in that OSM object, simply return from the
function. If you want to add the OSM object to some table call the `add_row()`
function on that table:

```
-- definition of the table:
table_pois = osm2pgsql.define_node_table('pois', {
    { column = 'tags', type = 'hstore' },
    { column = 'name', type = 'text' },
    { column = 'geom', type = 'point' },
})
...
function osm2pgsql.process_node(object)
...
    table_pois:add_row({
        tags = object.tags,
        name = object.tags.name
    })
...
end
```

The `add_row()` function takes a single table parameter, that describes what to
fill into all the database columns. (Any column not mentioned will be set to
`NULL`.) Note that you can't set the object id or geometry, this will be
handled for you behind the scenes.

## Stages and userdata

Osm2pgsql processes the data in up to two stages.

In "create" mode all objects read from the input file are processed in stage 1
only. In "append" mode objects read from the input file are processed in stage
1 while objects that are not in the input file but might change due to the
changes in the input file are processed in stage 2.

Your Lua script will be called twice, once in stage 1, once in stage 2. You
can look at `osm2pgsql.stage` to differentiate between the two. Any data
that you put into the `osm2pgsql.userdata` table in stage 1 will be available
at the same place in stage 2. Other data will be lost between the stages,
because a new Lua interpreter is started for each stage.

Stage 2 might run in several parallel threads depending on the setting of the
`--number-processes` option. Each of those threads has their own copy of the
user data, so changing it in stage 2 will probably not do what you want.

In addition to the objects processed in stage 2 because of the "append" mode
processing, you can add any way or relation to the "todo" list for stage 2.
Call `osm2pgsql.mark(type, id)` to mark an object for this list.

## Command line options

Use the command line option `-O flex` or `--output=flex` to enable the flex
backend and the `-S|--style` option to set the Lua config file.

The following command line options have a somewhat different meaning when
using the flex backend:

* `-p|--prefix`: The table names you are setting in your Lua config files
  will *not* get this prefix. You can easily add the prefix in the Lua config
  yourself.
* `-S|--style`: Use this to specify the Lua config file. Without it, osm2pgsql
  will not work, because it will try to read the default style file.
* `-G|--multi-geometry` is not used. Instead, set the type of the geometry
  column to the type you want, ie `polygon` vs. `multipolygon`.

The following command line options are ignored by `osm2pgsl` when using the
flex backend, because they don't make sense in that context:

* `-k|--hstore`
* `-j|--hstore-all`
* `-z|--hstore-column`
* `--hstore-match-only`
* `--hstore-add-index`
* `-K|--keep-coastlines` (Coastline tags are not handled specially in the
  flex backend.)
* `--tag-transform-script` (Set the Lua config file with the `-S|--style`
  option.)

## Example config files

This directory contains example config files for the flex backend. All config
files are documented extensively with inline comments.

If you are learning about the flex backend, read the config files in the
following order (from easiest to understand to the more complex ones):

1. [simple.lua](simple.lua)
2. [multipolygons.lua](multipolygons.lua)
3. [advanced.lua](advanced.lua)

These are some more advanced examples:

* [highway-shields.lua](highway-shields.lua)
* [route-relations.lua](route-relations.lua)
* [unitable.lua](unitable.lua)

The following config file tries to be more or less compatible with the old
osm2pgsql C transforms:

* [compatible.lua](compatible.lua)

