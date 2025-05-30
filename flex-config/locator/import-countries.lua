-- This config example file is released into the Public Domain.

-- This file is part of the examples for using the locator function of
-- osm2pgsql. It is used to import all country boundaries into a database.

local countries = osm2pgsql.define_relation_table('countries', {
    -- For the ISO3166-1 Alpha-2 country code
    -- https://en.wikipedia.org/wiki/ISO_3166-1
    { column = 'code', type = 'text', not_null = true },
    -- Because we want to use the geometries for the locator feature they
    -- must be in 4326! We use a polygon type here and will later split
    -- multipolygons into their parts.
    { column = 'geom', type = 'polygon', not_null = true, projection = 4326 },
})

function osm2pgsql.process_relation(object)
    local t = object.tags

    if t.boundary == 'administrative' and t.admin_level == '2' then
        local code = t['ISO3166-1']

        -- Ignore entries with syntactically invalid ISO code
        if not code or not string.match(code, '^%u%u$') then
            return
        end

        for geom in object:as_multipolygon():geometries() do
            countries:insert({
                code = code,
                geom = geom,
            })
        end
    end
end

