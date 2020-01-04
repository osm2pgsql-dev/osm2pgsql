
# The Flex Backend

**The Flex Backend is experimental. Everything in here is subject to change.**

The "Flex" backend, as the name suggests, allows for a more flexible
configuration that tells osm2pgsql what OSM data to store in your database and
exactly where and how. It is configured through a Lua file which

* defines the structure of the output tables and
* defines functions to map the OSM data to the database data format

See also the example config files in the `flex-config` directory which contain
lots of comments to get you started.

## The Lua config file

All configuration is done through the `osm2pgsql` object in Lua. It has the
following fields:

* `osm2pgsql.version`: The version of osm2pgsql as a string.
* `osm2pgsql.srid`: The SRID set on the command line (with `-l|--latlong`,
  `-m|--merc`, or `-E|--proj`).
* `osm2pgsql.mode`: Either `"create"` or `"append"` depending on the command
  line options (`--create` or `-a|--append`).
* `osm2pgsql.stage`: Either `1` or `2` (1st/2nd stage processing of the data).
  See below.

The following functions are defined:

* `osm2pgsql.define_node_table(name, columns)`: Define a node table.
* `osm2pgsql.define_way_table(name, columns)`: Define a way table.
* `osm2pgsql.define_relation_table(name, columns)`: Define a relation table.
* `osm2pgsql.define_area_table(name, columns)`: Define an area table.
* `osm2pgsql.define_table()`: Define a table. This is the more flexible
  function behind all the other `define_*_table()` functions. It gives you
  more control than the more convenient other functions.
* `osm2pgsql.mark_way(id)`: Mark the OSM way with the specified id. This way
  will be processed (again) in stage 2.
* `osm2pgsql.mark_relation(id)`: Mark the OSM relation with the specified id.
  This relation will be processed (again) in stage 2.

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
way, relation, or area, respectively. Osm2pgsql makes sure that the OSM object
id will be stored in the table so that later updates to those OSM objects (or
deletions) will be properly reflected in the tables. Area tables are special,
they can contain data derived from ways or from relations. Way ids will be
stored as is, relation ids will be stored as negative numbers.

With the `osm2pgsql.define_table()` function you can also define tables that
* don't have any ids, but those tables will never be updated by osm2pgsql
* take *any OSM object*, in this case the type of object is stored in an
  additional column.
* are in a specific PostgresSQL tablespace (set `data_tablespace`) or that
  get their indexes created in a specific tablespace (set `index_tablespace`).

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
* `point`: Point geometry, usually created from nodes.
* `linestring`: Linestring geometry, usually created from ways.
* `polygon`: Polygon geometry for area tables, created from ways or relations.
* `multipoint`: Currently not used.
* `multilinestring`: Created from (possibly split up) ways or relations.
* `multipolygon`: For area tables, created from ways or relations.
* `geometry`: Any kind of geometry. Also used for area tables that should hold
  both polygon and multipolygon geometries.

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
* `int4`, `int`, `integer`: 32bit signed integer. Values too large to fit will
  be truncated in some unspecified way.
* `int8`, `bigint`: 64bit signed integer. Values too large to fit will be
  truncated in some unspecified way.
* `real`: A real number.
* `hstore`: Automatically filled from a Lua table with only strings as keys
  and values.
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
return value. If you are not interested in all object types, you do not have
to supply all the functions.

These functions are called for each new or modified OSM object in the input
file. No function is called for deleted objects, osm2pgsql will automatically
delete all data in your database tables that derived from deleted objects.
Modifications are handled as deletions followed by creation of a "new" object,
for which the functions are called.

The parameter table (`object`) has the following fields:

* `id`: The id of the node, way, or relation.
* `tags`: A table with all the tags of the object.
* `version`, `timestamp`, `changeset`, `uid`, and `user`: Attributes of the
  OSM object. These are only available if the `-x|--extra-attributes` option
  is used and the OSM input file actually contains those fields. The
  `timestamp` contains the time in seconds since the epoch (midnight
  1970-01-01).
* `grab_tag(KEY)`: Return the tag value of the specified key and remove the
  tag from the list of tags. (Example: `local name = object:grab_tag('name')`)
  This is often used when you want to store some tags in special columns and
  the rest of the tags in an hstore column.
* `get_bbox()`: Get the bounding box of the current node or way. (It doesn't
  work for relations currently.)

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
        name = object.tags.name,
        geom = { create = 'point' }
    })
...
end
```

The `add_row()` function takes a single table parameter, that describes what to
fill into all the database columns. Any column not mentioned will be set to
`NULL`.

The geometry column in somewhat special. You have to define a *geometry
transformation* that will be used to transform the OSM object data into
a geometry that fits into the geometry column. See the next section for
details.

Note that you can't set the object id, this will be handled for you behind the
scenes.

## Geometry transformations

Currently these geometry transformations are supported:

* `{ create = 'point'}`. Only valid for nodes, create a 'point' geometry.
* `{ create = 'line'}`. For ways or relations. Create a 'linestring' or
  'multilinestring' geometry.
* `{ create = 'area'}` For ways or relations. Create a 'polygon' or
  'multipolygon' geometry.

Some of these transformations can have parameters:

* The `line` transformation has an optional parameter `split_at`. If this
  is set to anything other than 0, linestrings longer than this value will
  be split up into parts no longer than this value.
* The `area` transformation has an optional parameter `multi`. If this is
  set to `false` (the default), a multipolygon geometry will be split up into
  several polygons. If this is set to `true`, the multipolygon geometry is
  kept as one. It depends on this parameter whether you need a polygon
  or multipolygon geometry column.

If no geometry transformation is set, osm2pgsql will, in some cases, assume
a default transformation. These are the defaults:

* For node tables, a `point` column gets the node location.
* For way tables, a `linestring` column gets the complete way geometry, a
  `polygon` column gets the way geometry as area (if the way is closed and
  the area is valid).

## Stages

Osm2pgsql processes the data in up to two stages. You can mark ways or
relations in stage 1 for processing in stage 2 by calling
`osm2pgsql.mark_way(id)` or `osm2pgsql.mark_relation(id)`, respectively. If you
don't mark any objects, nothing will be done in stage 2.

You can look at `osm2pgsql.stage` to see in which stage you are.

In stage 1 you can only look at each OSM object on its own. You can see
its id and tags (and possibly timestamp, changeset, user, etc.), but you don't
know how this OSM objects relates to other OSM objects (for instance whether a
way you are looking at is a member in a relation). If this is enough to decide
in which database table(s) and with what data an OSM object should end up in,
then you can process the OSM object in stage 1. If, on the other hand, you
need some extra information, you have to defer processing to the second stage.

You want to do all the processing you can in stage 1, because it is faster
and there is less memory overhead. For most use cases, stage 1 is enough. If
it is not, use stage 1 to store information about OSM objects you will need
in stage 2 in some global variable. In stage 2 you can read this information
again and use it to decide where and how to store the data in the database.

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
* `-G|--multi-geometry` (Use the `multi` option on the geometry transformation
  instead.)
* The command line options to set the tablespace are ignored by the flex
  backend, instead use the `data_tablespace` or `index_tablespace` options
  when defining your table.

