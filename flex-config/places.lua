-- This config example file is released into the Public Domain.

-- This example shows how you can use JSON and JSONB columns in your database.

local places = osm2pgsql.define_node_table('places', {
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

    -- The population should be stored as number, not as a string
    if object.tags.population then
        object.tags.population = tonumber(object.tags.population)
    end

    places:insert({
        tags = object.tags,
        geom = object:as_point()
    })
end

