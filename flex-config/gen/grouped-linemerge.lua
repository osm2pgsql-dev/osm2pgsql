-- This config example file is released into the Public Domain.
--
-- This Lua config demonstrates the 'grouped-linemerge' generalization. It
-- merges connected lines that share the same set of grouping columns into
-- single (multi-)lines, the equivalent of
--
--   SELECT cols..., (ST_Dump(ST_LineMerge(ST_Collect(geom)))).geom
--     FROM roads GROUP BY cols...
--
-- but done globally and maintained incrementally on updates. A typical use is
-- merging road segments that render identically (same name/ref/highway/layer)
-- so that labels and route shields are placed on the whole road instead of on
-- each individual OSM way, without the artifacts you get when merging only
-- within a tile.
--
-- NOTE THAT THE GENERALIZATION SUPPORT IS EXPERIMENTAL AND MIGHT CHANGE
-- WITHOUT NOTICE!
--
-- Workflow:
--   * Import as usual:           osm2pgsql -O flex -S grouped-linemerge.lua DATA.osm.pbf
--   * Build the merged table:    osm2pgsql-gen -S grouped-linemerge.lua
--   * Apply an update:           osm2pgsql -a -O flex -S grouped-linemerge.lua CHANGES.osc.gz
--   * Update the merged table:   osm2pgsql-gen -a -S grouped-linemerge.lua

-- An expire output records which tiles changed during an update. The
-- grouped-linemerge generalization uses it only as a seed for "where did line
-- geometry change" - it then walks each affected connected component out from
-- there and re-merges it. Use a high maxzoom so the seed regions are small.
local exp_roads = osm2pgsql.define_expire_output({
    maxzoom = 18,
    table = 'exp_roads',
})

-- The source table with the original road segments (one row per OSM way).
local roads = osm2pgsql.define_table({
    name = 'roads',
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'name', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'layer', type = 'int' },
        -- Attach the expire output to the geometry so that any change to a
        -- road's geometry (add/modify/delete) expires the tiles it covers.
        { column = 'geom', type = 'linestring', not_null = true,
            expire = { { output = exp_roads } } },
    }
})

-- The destination table with the merged roads. Its columns are exactly the
-- grouping columns plus the geometry. It has no OSM id column (it is derived
-- data maintained by osm2pgsql-gen, not by the normal update process); the
-- warning osm2pgsql prints about that is expected.
osm2pgsql.define_table({
    name = 'roads_merged',
    columns = {
        { column = 'name', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'layer', type = 'int' },
        { column = 'geom', type = 'linestring', not_null = true },
    }
})

function osm2pgsql.process_way(object)
    local highway = object.tags.highway
    if not highway then
        return
    end
    roads:insert({
        name = object.tags.name,
        ref = object.tags.ref,
        highway = highway,
        layer = tonumber(object.tags.layer),
        geom = object:as_linestring(),
    })
end

function osm2pgsql.process_gen()
    osm2pgsql.run_gen('grouped-linemerge', {
        name = 'roads',          -- name (for logging)
        debug = false,           -- set to true for more detailed debug output
        src_table = 'roads',     -- input table with the line segments
        dest_table = 'roads_merged', -- output table for the merged lines
        geom_column = 'geom',    -- geometry column (same in src and dest)

        -- Lines are merged when ALL of these columns are equal (NULLs compare
        -- equal). Pass them as a comma-separated list.
        group_by_columns = 'name, ref, highway, layer',

        -- Optional pre-filter (SQL boolean expression on the source columns).
        -- Lines not matching are completely excluded from the generalization.
        -- Here we only merge roads that carry a label or a shield.
        where = 'name IS NOT NULL OR ref IS NOT NULL',

        -- In append mode, where to read the expired tiles from, and the zoom
        -- level they were captured at (must match the expire output's maxzoom).
        expire_list = 'exp_roads',
        zoom = 18,

        -- Create functional endpoint indexes on the src/dest tables in create
        -- mode. These make the incremental component walk fast. Set to false
        -- if you manage the indexes yourself.
        create_indexes = true,
    })
end
