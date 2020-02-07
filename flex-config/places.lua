
-- This example shows how you can use JSON and JSONB columns in your database.

-- You need the 'dkjson' JSON library for Lua. Install 'dkjson' with LuaRocks
-- or install from http://dkolf.de/src/dkjson-lua.fsl/home . Debian/Ubuntu
-- users can install the 'lua-dkjson' package.

-- Use JSON encoder
local json = require('dkjson')

local places = osm2pgsql.define_node_table('places', {
    -- The jsonb column is handled by osm2pgsql just like a normal text
    -- column. It is the job of the Lua script to put correct json there.
    { column = 'tags', type = 'jsonb' },
    { column = 'geom', type = 'point' },
})

local function starts_with(str, start)
   return str:sub(1, #start) == start
end

function osm2pgsql.process_node(object)
    if not object.tags.place then
        return
    end

    -- Put all name:* tags in their own substructure
    local names = {}
    local has_names = false
    for k, v in pairs(object.tags) do
        if k == 'name' then
            names[''] = v
            object.tags.name = nil
            has_names = true
        elseif starts_with(k, "name:") then
            -- extract language
            local lang = k:sub(6, -1)
            names[lang] = v
            object.tags[k] = nil
            has_names = true
        end
    end

    if has_names then
        object.tags.names = names
    end

    places:add_row({
        tags = json.encode(object.tags)
    })
end

