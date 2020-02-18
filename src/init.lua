--
--  This will be compiled into osm2pgsql and run as initialization code in
--  the flex backend.
--

local math = require('math')

local _define_table_impl = function (_type, _name, _columns)
    return osm2pgsql.define_table{
        name = _name,
        ids = { type = _type, id_column = _type .. '_id' },
        columns = _columns,
    }
end

function osm2pgsql.define_node_table(_name, _columns)
    return _define_table_impl('node', _name, _columns)
end

function osm2pgsql.define_way_table(_name, _columns)
    return _define_table_impl('way', _name, _columns)
end

function osm2pgsql.define_relation_table(_name, _columns)
    return _define_table_impl('relation', _name, _columns)
end

function osm2pgsql.define_area_table(_name, _columns)
    return _define_table_impl('area', _name, _columns)
end

function osm2pgsql.mark_way(id)
    return osm2pgsql.mark('w', id)
end

function osm2pgsql.mark_relation(id)
    return osm2pgsql.mark('r', id)
end

function osm2pgsql.clamp(value, low, high)
    return math.min(math.max(value, low), high)
end

function osm2pgsql.make_check_values_func(list, default)
    local valid_values = {}
    if default ~= nil then
        local mt = {__index = function () return default end}
        setmetatable(valid_values, mt)
    end

    for _, elem in ipairs(list) do
        valid_values[elem] = elem
    end

    return function(value)
        return valid_values[value]
    end
end

function osm2pgsql.make_clean_tags_func(keys)
    local keys_to_delete = {}
    local prefixes_to_delete = {}

    for _, k in ipairs(keys) do
        if k:sub(-1) == '*' then
            prefixes_to_delete[#prefixes_to_delete + 1] = k:sub(1, -2)
        else
            keys_to_delete[#keys_to_delete + 1] = k
        end
    end

    return function(tags)
        for _, k in ipairs(keys_to_delete) do
            tags[k] = nil
        end

        if next(tags) == nil then
            return true
        end

        for tag, _ in pairs(tags) do
            for _, k in ipairs(prefixes_to_delete) do
                if tag:sub(1, k:len()) == k then
                    tags[tag] = nil
                    break
                end
            end
        end

        return next(tags) == nil
    end
end

-- This will be the metatable for the OSM objects given to the process callback
-- functions.
local inner_metatable = {
    __index = function(table, key)
        if key == 'version' or key == 'timestamp' or
           key == 'changeset' or key == 'uid' or key == 'user' then
            return nil
        end
        error("unknown field '" .. key .. "'", 2)
    end
}

object_metatable = {
    __index =  {
        grab_tag = function(data, tag)
            if not tag then
                error("Missing tag key", 2)
            end
            local v = data.tags[tag]
            data.tags[tag] = nil
            return v
        end
    }
}

setmetatable(object_metatable.__index, inner_metatable)

