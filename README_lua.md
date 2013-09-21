# Lua tag transformations

osm2pgsql supports [Lua](http://lua.org/) scripts to rewrite tags before they enter the database.

This allows you to unify disparate tagging (for example, `highway=path; foot=yes` and `highway=footway`) and perform complex queries, potentially more efficiently than writing them as rules in your Mapnik or other stylesheet.

## How to

Pass a Lua script to osm2pgsql using the command line switch `--tag-transform-script`:

    osm2pgsql -S your.style --tag-transform-script your.lua --hstore-all extract.osm.pbf

This Lua script needs to implement the following functions:

    function filter_tags_node(tags, num_tags)
    return filter, tags

    function filter_tags_way(tags, num_tags)
    return filter, tags, polygon, roads

    function filter_basic_tags_rel(tags, num_tags)
    return filter, tags

These take a set of tags as a Lua key-value table, and an integer which is the number of tags supplied.

The first return value is `filter`, a flag which you should set to `1` if the way/node/relation should be filtered out and not added to the database, `0` otherwise. (They will still end up in the slim mode tables, but not in the rendering tables)

The second return value is `tags`, a transformed (or unchanged) set of tags.

`filter_tags_way` returns two additional flags. `poly` should be `1` if the way should be treated as a polygon, `0` as a line. `roads` should be `1` if the way should be added to the planet_osm_roads table, `0` otherwise.

    function filter_tags_relation_member(tags, member_tags, 
        roles, num_members)
    return filter, tags, member_superseded, boundary, 
        polygon, roads

The function filter_tags_relation_member is more complex and can handle more advanced relation tagging, such as multipolygons that take their tags from the member ways.

This function is called with the tags from the relation; an set of tags for each of the member ways (member relations and nodes are ignored); the set of roles for each of the member ways; and the number of members. The tag and role sets are both arrays (indexed tables) of hashes (tables).

As usual, it should return a filter flag, and a transformed set of tags to be applied to the relation in later processing.

The third return value, `member_superseded`, is a flag set to `1` if the way has now been dealt with (e.g. outer ways in multipolygon relations, which are superseded by the multipolygon geometry), `0` if it needs to have its own entry in the database (e.g. tagged inner ways).

The fourth and fifth return values, `boundary` and `polygon`, are flags that specify if the relation should be processed as a line, a polygon, or both (e.g. administrative boundaries).

The final return value, `roads`, is `1` if the geometry should be added to the `planet_osm_roads` table.

There is a sample tag transform lua script in the repository as an example, which (nearly) replicates current processing and can be used as a template for one's own scripts.

## In practice

There is inevitably a performance hit with any extra processing. The sample Lua tag transformation is a little slower than the C-based default. However, extensive Lua pre-processing may save you further processing in your Mapnik (or other) stylesheet.

Test your Lua script with small excerpts before applying it to a whole country or even the planet.

Where possible, add new tags, don't replace existing ones; otherwise you will be faced with a reimport if you decide to change your transformation.
