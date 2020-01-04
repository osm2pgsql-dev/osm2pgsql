
inspect = require('inspect')

-- We define a single table that can take any OSM object and any geometry.
-- XXX Updates/expire will currently not work on these tables.
local dtable = osm2pgsql.define_table{
    name = "data",
    -- This will generate a column "osm_id INT8" for the id, and a column
    -- "osm_type CHAR(1)" for the type of object: N(ode), W(way), R(relation)
    ids = { type = 'any', id_column = 'osm_id', type_column = 'osm_type' },
    columns = {
        { column = 'attrs', type = 'hstore' },
        { column = 'tags', type = 'hstore' },
        { column = 'geom', type = 'geometry' },
    }
}

print("columns=" .. inspect(dtable:columns()))

function is_empty(some_table)
    return next(some_table) == nil
end

function clean_tags(tags)
    tags.odbl = nil
    tags.created_by = nil
    tags.source = nil
    tags["source:ref"] = nil
    tags["source:name"] = nil
end

function process(object)
    clean_tags(object.tags)
    if is_empty(object.tags) then
        return
    end
    dtable:add_row({
        attrs = {
            version = object.version,
            timestamp = object.timestamp,
        },
        tags = object.tags
    })
end

osm2pgsql.process_node = process
osm2pgsql.process_way = process

function osm2pgsql.process_relation(object)
    clean_tags(object.tags)
    if is_empty(object.tags) then
        return
    end

    if object.tags.type == 'multipolygon' or object.tags.type == 'boundary' then
        dtable:add_row({
            attrs = {
                version = object.version,
                timestamp = object.timestamp,
            },
            tags = object.tags
        })
    end
end

