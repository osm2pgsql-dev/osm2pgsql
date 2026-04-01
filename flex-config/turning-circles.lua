-- This config example file is released into the Public Domain.

-- Create a table with turning circles that can be styled in sync with the
-- highway they are on.

local turning_circles = osm2pgsql.define_table({
    name = 'turning_circles',
    ids = { type = 'node', id_column = 'node_id', cache = true },
    columns = {
        { column = 'geom', type = 'point', not_null = true },
    }
})

local highways = osm2pgsql.define_table({
    name = 'highways',
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'htype', type = 'text', not_null = true },
        { column = 'geom', type = 'linestring', not_null = true },
    }
})

-- This table will contain entries for all node/way combinations where the way
-- is tagged as "highway" and the node is tagged as "highway=turning_circle".
-- The "htype" column contains the highway type, the "geom" the geometry of
-- the node. This can be used, for instance, to draw the point in a style that
-- fits with the style of the highway.
--
-- Note that you might have multiple entries for the same node in this table
-- if it is in several ways. In that case you might have to decide at rendering
-- time which of them to render.
local highway_ends = osm2pgsql.define_table({
    name = 'highway_ends',
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'htype', type = 'text', not_null = true },
        { column = 'node_id', type = 'int8', not_null = true },
        { column = 'geom', type = 'point', not_null = true },
    }
})

function osm2pgsql.process_node(object)
    if object.tags.highway == 'turning_circle' then
        -- This insert will add the entry to the id cache later read with
        -- in_id_cache().
        turning_circles:insert({
            geom = object:as_point(),
        })
    end
end

function osm2pgsql.process_way(object)
    local t = object.tags.highway
    if t then
        highways:insert({
            htype = t,
            geom = object:as_linestring(),
        })
        local c = turning_circles:in_id_cache(object.nodes)
        for _, n in ipairs(c) do
            highway_ends:insert({
                htype = t,
                node_id = object.nodes[n],
                geom = object:as_point(n),
            })
        end
    end
end

