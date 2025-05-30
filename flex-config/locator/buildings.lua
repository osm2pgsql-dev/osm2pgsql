-- This config example file is released into the Public Domain.

-- This file shows how to use a locator to tag all buildings with the country
-- they are in.

-- Define the "countries" locator and get all country geometries from the
-- database. Use the import-countries.lua file to import them first, before
-- you run this.
local countries = osm2pgsql.define_locator({ name = 'countries' })

-- The SELECT query must return the regions the locator will use. The first
-- column must contain the name of the region, the second the geometry in
-- WGS84 lon/lat coordinates.
-- To improve the efficiency of the country lookup, we'll subdivide the
-- country polygons into smaller pieces. (If you run this often, for instance
-- when doing updates, do the subdivide first in the database and store the
-- result in its own table.)
countries:add_from_db('SELECT code, ST_Subdivide(geom, 200) FROM countries')

-- You have to decide whether you are interested in getting all regions
-- intersecting with any of the objects or only one of them.
--
-- * Getting all regions makes sure that you get everything, even if regions
--   overlap or objects straddle the border between regions. Use the function
--   all_intersecting() for that.
-- * Getting only one region is faster because osm2pgsql can stop looking
--   for matches after the first one. Use first_intersecting() for that.
--   This makes sense if you only have a single region anyway or if your
--   regions don't overlap or you are not so concerned with what happens at
--   the borders.
--
-- Just for demonstration, we do both in this example, in the "country" and
-- "countries" columns, respectively.
local buildings = osm2pgsql.define_area_table('buildings', {

    -- This will contain the country code of the first matching country
    -- (which can be any of the countries because there is no order here).
    { column = 'country', type = 'text' },

    -- This array will contain the country codes of all matching countries.
    { column = 'countries', sql_type = 'text[]' },
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'polygon', not_null = true },
})

local function add(geom, tags)
    buildings:insert({
        country = countries:first_intersecting(geom), -- or use geom:centroid()

        -- We have to create the format that PostgreSQL expects for text
        -- arrays. We assume that the region names do not contain any special
        -- characters, otherwise we would need to do some escaping here.
        countries = '{' .. table.concat(countries:all_intersecting(geom), ',') .. '}',

        tags = tags,
        geom = geom,
    })
end

function osm2pgsql.process_way(object)
    if object.tags.building then
        add(object:as_polygon(), object.tags)
    end
end

function osm2pgsql.process_relation(object)
    if object.tags.building then
        local geom = object:as_multipolygon()
        for p in geom:geometries() do
            add(p, object.tags)
        end
    end
end

