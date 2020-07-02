
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
* `osm2pgsql.mode`: Either `"create"` or `"append"` depending on the command
  line options (`--create` or `-a|--append`).
* `osm2pgsql.stage`: Either `1` or `2` (1st/2nd stage processing of the data).
  See below.

The following functions are defined:

* `osm2pgsql.define_node_table(name, columns[, options])`: Define a node table.
* `osm2pgsql.define_way_table(name, columns[, options])`: Define a way table.
* `osm2pgsql.define_relation_table(name, columns[, options])`: Define a relation
  table.
* `osm2pgsql.define_area_table(name, columns[, options])`: Define an area table.
* `osm2pgsql.define_table(options)`: Define a table. This is the more flexible
  function behind all the other `define_*_table()` functions. It gives you
  more control than the more convenient other functions.

You are expected to define one or more of the following functions:

* `osm2pgsql.process_node()`: Called for each new or changed node.
* `osm2pgsql.process_way()`: Called for each new or changed way.
* `osm2pgsql.process_relation()`: Called for each new or changed relation.
* `osm2pgsql.select_relation_members()`: Called for each deleted or added
  relation. See below for more details.

Osm2pgsql also provides some additional functions in the
[lua-lib.md](Lua helper library).

### Defining a table

You have to define one or more tables where your data should end up. This
is done with the `osm2pgsql.define_table()` function or one of the slightly
more convenient functions `osm2pgsql.define_(node|way|relation|area)_table()`.

```
osm2pgsql.define_table(OPTIONS)

osm2pgsql.define_(node|way|relation|area)_table(NAME, COLUMNS[, OPTIONS])
```

Here NAME is the name of the table, COLUMNS is a list of Lua tables describing
the columns as documented below. OPTIONS is a Lua table with options for
the table as a whole. When using the `define_table()` command, the NAME and
COLUMNS are specified as options "name" and "columns", respectively.

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
  additional `char(1)` column.
* are in a specific PostgresSQL tablespace (set option `data_tablespace`) or
  that get their indexes created in a specific tablespace (set option
  `index_tablespace`).
* are in a specific schema (set option `schema`). Note that the schema has to
  be created before you start osm2pgsql.

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

By default geometry columns will be created in web mercator (EPSG 3857). To
change this, set the `projection` parameter of the column to the EPSG code
you want (or one of the strings `latlon(g)`, `WGS84`, or 'merc(ator)', case
is ignored).

There is one special geometry column type called `area`. It can be used in
addition to a `polygon` or `multipolygon` column. Unlike the normal geometry
column types, the resulting database type will not be a geometry type, but
`real`. It will be filled automatically with the area of the geometry. The area
will be calculated in web mercator, or you can set the `projection` parameter
of the column to `4326` to calculate it with WGS84 coordinates. Other
projections are currently not supported.

In addition to id and geometry columns, each table can have any number of
"normal" columns using any type supported by PostgreSQL. Some types are
specially recognized by osm2pgsql: `text`, `boolean`, `int2` (`smallint`),
`int4` (`int`, `integer`), `int8` (`bigint`), `real`, `hstore`, and
`direction`. See the "Type conversion" section for details.

Instead of the above types you can use any SQL type you want. If you do that
you have to supply the PostgreSQL string representation for that type when
adding data to such columns (or Lua nil to set the column to `NULL`).

In the table definitions the columns are specified as a list of Lua tables
with the following keys:

* `column`: The name of the PostgreSQL column (required).
* `type`: The type of the column as described above (required).
* `not_null = true`: Make this a `NOT NULL` column. (Optional, default `false`.)
* `create_only = true`: Add the column to the `CREATE TABLE` command, but
  do not try to fill this column when adding data. This can be useful for
  `SERIAL` columns or when you want to fill in the column later yourself.
  (Optional, default `false`.)

All the `osm2pgsql.define*table()` functions return a database table object.
You can call the following functions on it:

* `name()`: The name of the table as specified in the define function.
* `schema()`: The schema of the table as specified in the define function.
* `columns()`: The columns of the table as specified in the define function.
* `add_row()`: Add a row to the database table. See below for details.

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
* `get_bbox()`: Get the bounding box of the current node or way.
  This function returns four result values: the lot/lat values for the
  bottom left corner of the bounding box, followed by the lon/lat values
  of the top right corner. Both lon/lat values are identical in case of nodes.
  Example: `lon, lat, dummy, dummy = object.get_bbox()`
  (This function doesn't work for relations currently.)

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

When processing OSM data, osm2pgsql reads the input file(s) in order, nodes
first, then ways, then relations. This means that when the ways are read and
processed, osm2pgsql can't know yet whether a way is in a relation (or in
several). But for some use cases we need to know in which relations a way is
and what the tags of these relations are or the roles of those member ways.
The typical case are relations of type `route` (bus routes etc.) where we
might want to render the `name` or `ref` from the route relation onto the
way geometry.

The osm2pgsql flex backend supports this use case by adding an additional
"reprocessing" step. Osm2pgsql will call the Lua function
`osm2pgsql.select_relation_members()` for each added, modified, or deleted
relation. Your job is to figure out which way members in that relation might
need the information from the relation to be rendered correctly and return
those ids in a Lua table with the only field 'ways'. This is usually done with
a function like this:

```
function osm2pgsql.select_relation_members(relation)
    if relation.tags.type == 'route' then
        return { ways = osm2pgsql.way_member_ids(relation) }
    end
end
```

Instead of using the helper function `osm2pgsql.way_member_ids()` which
returns the ids of all way members, you can write your own code, for instance
if you want to check the roles.

Note that `select_relation_members()` is called for deleted relations and for
the old version of a modified relation as well as for new relations and the
new version of a modified relation. This is needed, for instance, to correctly
mark member ways of deleted relations, because they need to be updated, too.
The decision whether a way is to be marked or not can only be based on the
tags of the relation and/or the roles of the members. If you take other
information into account, updates might not work correctly.

In addition you have to store whatever information you need about the relation
in your `process_relation()` function in a global variable.

After all relations are processed, osm2pgsql will reprocess all marked ways by
calling the `process_way()` function for them again. This time around you have
the information from the relation in the global variable and can use it.

If you don't mark any ways, nothing will be done in this reprocessing stage.

(It is currently not possible to mark nodes or relations. This might or might
not be added in future versions of osm2pgsql.)

You can look at `osm2pgsql.stage` to see in which stage you are.

You want to do all the processing you can in stage 1, because it is faster
and there is less memory overhead. For most use cases, stage 1 is enough.

Processing in two stages can add quite a bit of overhead. Because this feature
is new, there isn't much operational experience with it. So be a bit careful
when you are experimenting and watch memory and disk space consumption and
any extra time you are using. Keep in mind that:

* All data stored in stage 1 for use in stage 2 in your Lua script will use
  main memory.
* Keeping track of way ids marked in stage 1 needs some memory.
* To do the extra processing in stage 2, time is needed to get objects out
  of the object store and reprocess them.
* Osm2pgsql will create an id index on all way tables to look up ways that
  need to be deleted and re-created in stage 2.

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
* `-l|--latlong` (Set `projection = 'latlong'` on geometry columns instead.)
* `-m|--merc` (This is the default for all geometry columns.)
* `-E|--proj NUM` (Set `projection = NUM` on geometry columns instead.)
* `--reproject-area` (This is the default, set `projection = 4326` on the area
  column to calculate the area using WGS84 coordinates.)
* `-G|--multi-geometry` (Use the `multi` option on the geometry transformation
  instead, see the "Geometry transformation" section above for details.)
* The command line options to set the tablespace (`-i|--tablespace-index`,
  `--tablespace-main-data`, `--tablespace-main-index`) are ignored by the flex
  backend, instead use the `data_tablespace` or `index_tablespace` options when
  defining your table.
* `--output-pgsql-schema` (Use the `schema` option on the table definitions
  instead.)

## Type conversions

The `add_row()` command will try its best to convert Lua values into
corresponding PostgreSQL values. But not all conversions make sense. Here
are the detailed rules:

1. Lua values of type `function`, `userdata`, or `thread` will always result in
   an error.
2. The Lua type `nil` is always converted to `NULL`.
3. If the result of a conversion is `NULL` and the column is defined as `NOT
   NULL`, an error is thrown.
4. The Lua type `table` is converted to the PostgreSQL type `hstore` if and
   only if all keys and values in the table are string values. A Lua `table`
   can not be converted to any other PostgreSQL type.
5. For `boolean` columns: The number `0` is converted to `false`, all other
   numbers are `true`. Strings are converted as follows: `"yes"`, `"true"`,
   `"1"` are `true`; `"no"`, `"false"`, `"0"` are `false`, all others are
   `NULL`.
6. For integer columns (`int2`, `int4`, `int8`): Boolean `true` is converted
   to `1`, `false` to `0`. Numbers that are not integers or outside the range
   of the type result in `NULL`. Strings are converted to integers if possible
   otherwise the result is `NULL`.
7. For `real` columns: Booleans result in an error, all numbers are used as
   is, strings are converted to a number, if that is not possible the result
   is `NULL`.
8. For `direction` columns (stored as `int2` in the database): Boolean `true`
   is converted to `1`, `false` to `0`. The number `0` results in `0`, all
   positive numbers in `1`, all negative numbers in `-1`. Strings `"yes"` and
   `"1"` will result in `1`, `"no"` and `"0"` in `0`, `"-1"` in `-1`. All
   other strings will result in `NULL`.
9. For text columns and any other not specially recognized column types,
   booleans result in an error and numbers are converted to strings.

If you want any other conversions, you have to do them yourself in your Lua
code. Osm2pgsql provides some helper functions for other conversions, see
the (lua-lib Documentation)[lua-lib.md].

