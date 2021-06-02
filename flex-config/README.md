# Flex Output Configuration

See the [Flex Output](https://osm2pgsql.org/doc/manual.html#the-flex-output)
chapter in the manual for all the details.

## Example config files

This directory contains example config files for the flex output. All config
files are documented extensively with inline comments.

If you are learning about the flex output, read the config files in the
following order (from easiest to understand to the more complex ones):

1. [simple.lua](simple.lua) -- Introduction to config file format
2. [geometries.lua](geometries.lua) -- Geometry column options
3. [data-types.lua](data-types.lua) -- Data types and how to handle them

After that you can dive into more advanced topics:

* [route-relations.lua](route-relations.lua) -- Use multi-stage processing
  to bring tags from relations to member ways
* [unitable.lua](unitable.lua) -- Put all OSM data into a single table
* [places.lua](places.lua) -- Creating JSON/JSONB columns
* [with-schema.lua](with-schema.lua) -- Use a database schema
* [attributes.lua](attributes.lua) -- How to access OSM object attributes

The "generic" configuration is a full-featured but simple configuration that
is a good starting point for your own real-world configuration:

* [generic.lua](generic.lua)

The following config file tries to be more or less compatible with the old
pgsql (C transform) output:

* [compatible.lua](compatible.lua)

## Dependencies

Some of the example files use the `inspect` Lua library to show debugging
output. It is not needed for the actual functionality of the examples, so if
you don't have the library, you can remove all uses of `inspect` and the
scripts should still work.

The library is available from [the
source](https://github.com/kikito/inspect.lua) or using
[LuaRocks](https://luarocks.org/modules/kikito/inspect). Debian/Ubuntu users
can install the `lua-inspect` package.

## Public Domain

All the example config files in this directory are released into the Public
Domain. You may use them in any way you like.

