-- This config example file is released into the Public Domain.

-- This is a very simple Lua config for the Flex output not intended for
-- real-world use. Look at and understand "simple.lua" first, before looking
-- at this file. This file demonstrates some column data type options.

local highways = osm2pgsql.define_way_table('highways', {
    { column = 'name',     type = 'text' },
    -- We always need a highway type, so we can declare the column as NOT NULL
    { column = 'type',     type = 'text', not_null = true },

    -- Add a SERIAL column and tell osm2pgsql not to fill it (PostgreSQL will
    -- do that for us)
    { column = 'id',       sql_type = 'serial', create_only = true },

    -- type "direction" is special, see below
    { column = 'oneway',   type = 'direction' },
    { column = 'maxspeed', type = 'int' },

    -- type "bool" is special, see below
    { column = 'lit',      type = 'bool' },
    { column = 'tags',     type = 'jsonb' }, -- also available: 'json', 'hstore'

    -- osm2pgsql doesn't know about PostgreSQL arrays, so we define the SQL
    -- type of this column and then have to convert our array data into a
    -- valid text representation for that type, see below.
    { column = 'nodes',    sql_type = 'int8[]' },
    { column = 'geom',     type = 'linestring' },
})

-- Helper function to remove some of the tags we usually are not interested in.
-- Returns true if there are no tags left.
local function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags['source:ref'] = nil

    return next(tags) == nil
end

local highway_types = {
    'motorway',
    'motorway_link',
    'trunk',
    'trunk_link',
    'primary',
    'primary_link',
    'secondary',
    'secondary_link',
    'tertiary',
    'tertiary_link',
    'unclassified',
    'residential',
    'track',
    'service',
}

-- Prepare table "types" for quick checking of highway types
local types = {}
for _, k in ipairs(highway_types) do
    types[k] = 1
end

-- Parse a maxspeed value like "30" or "55 mph" and return a number in km/h
local function parse_speed(input)
    if not input then
        return nil
    end

    local maxspeed = tonumber(input)

    -- If maxspeed is just a number, it is in km/h, so just return it
    if maxspeed then
        return maxspeed
    end

    -- If there is an 'mph' at the end, convert to km/h and return
    if input:sub(-3) == 'mph' then
        local num = tonumber(input:sub(1, -4))
        if num then
            return math.floor(num * 1.60934)
        end
    end

    return nil
end

function osm2pgsql.process_way(object)
    if clean_tags(object.tags) then
        return
    end

    -- Get the type of "highway" and remove it from the tags
    local highway_type = object:grab_tag('highway')

    -- We are only interested in highways of the given types
    if not types[highway_type] then
        return
    end

    -- We want to put the name in its own column
    local name = object:grab_tag('name')

    highways:insert({
        name = name,
        type = highway_type,

        -- The 'maxspeed' column gets the maxspeed in km/h
        maxspeed = parse_speed(object.tags.maxspeed),

        -- The 'oneway' column has the special type "direction", which will
        -- store "yes", "true" and "1" as 1, "-1" as -1, and everything else
        -- as 0.
        oneway = object.tags.oneway or 0,

        -- The 'lit' column has the special type "bool", which will store
        -- "yes" and "true" as true and everything else as false value.
        lit = object.tags.lit,

        -- The way node ids are put into a format that PostgreSQL understands
        -- for a column of type "int8[]".
        nodes = '{' .. table.concat(object.nodes, ',') .. '}',

        tags = object.tags,
        geom = object:as_linestring()
    })
end

