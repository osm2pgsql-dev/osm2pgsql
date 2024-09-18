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

* [public-transport.lua](public-transport.lua) -- Use multi-stage processing
  to bring tags from public transport relations to member nodes and ways
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

The other files demonstrate some specific functionality, look at these if
and when you need that functionality.

* [addresses.lua](addresses.lua) -- Get all objects with addresses and store
  as point objects in the database
* [bbox.lua](bbox.lua) -- Use of the `get_bbox()` function to get the bounding
  boxes of features
* [expire.lua](expire.lua) -- Tile expiry configuration
* [indexes.lua](indexes.lua) -- Various options around index creation
* [labelpoint.lua](labelpoint.lua) -- How to get good labelling points using
  the `centroid()` and `pole_of_inaccessibility()` functions.
* [untagged](untagged.lua) -- How to access untagged objects.

The subdirectory [gen](gen/) contains example configurations for
generalization support.

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

