-- This config example file is released into the Public Domain.

-- This file shows how to use a locator to find the country-specific colour

-- Define the "countries" locator and get all country geometries from the
-- database. Use the import-countries.lua file to import them first, before
-- you run this.
local countries = osm2pgsql.define_locator({ name = 'countries' })
countries:add_from_db('SELECT code, ST_Subdivide(geom, 200) FROM countries')

local highways = osm2pgsql.define_way_table('highways', {
    { column = 'hwtype', type = 'text' },
    { column = 'country', type = 'text' },
    { column = 'colour', type = 'text' },
    { column = 'geom', type = 'linestring', not_null = true },
})

-- Each country uses their own colour for motorways. Here is the beginning
-- of a list of some countries in Europe. Source:
-- https://en.wikipedia.org/wiki/Comparison_of_European_road_signs
local cc2colour = {
    BE = '#2d00e5',
    CH = '#128044',
    DE = '#174688',
    FR = '#333b97',
    NL = '#064269',
}

function osm2pgsql.process_way(object)
    if object.tags.highway then
        local geom = object:as_linestring()
        local cc = countries:first_intersecting(geom)
        highways:insert({
            hwtype = object.tags.highway,
            country = cc,
            colour = cc2colour[cc],
            geom = geom,
        })
    end
end
