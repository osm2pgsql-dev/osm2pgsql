-- This config example file is released into the Public Domain.

-- This config shows how to track in a table which OSM object have been
-- added, changed and deleted.

-- The main table logging the changes.
local change_table = osm2pgsql.define_table{
    name = 'change_log',
    -- Disable automatic ID tracking by osm2pgsql. No rows should ever
    -- be deleted. osm2pgsql will issue a warning about this. It can
    -- be safely ignored.
    ids = nil,
    columns = {
        { column = 'osm_type', type = 'text' },
        { column = 'osm_id', type = 'bigint' },
        { column = 'version', type = 'int' },
        -- This column describes the kind of change:
        -- 'A' for added/newly created,
        -- 'M' for modified,
        -- 'D' for deleted
        { column = 'action', type = 'text' },
        { column = 'date', sql_type = 'timestamp' }
    },
    indexes = {
        { column = { 'osm_type', 'osm_id' }, method = 'btree' }
    }
}

-- We only want to catch changes coming from the OSM file input.
-- This flag marks when file reading is done and dependent objects are
-- being processed.
local file_reading_in_progress = true

local function format_date(ts)
    return os.date('!%Y-%m-%dT%H:%M:%SZ', ts)
end

local function add_object_change(object)
    -- In this example only changes while updating the database are recorded.
    -- This happens in 'append' mode.
    if osm2pgsql.mode == 'append' and file_reading_in_progress then
        change_table:insert{
            osm_type = object.type,
            osm_id = object.id,
            version = object.version,
            action = (object.version == 1) and 'A' or 'M',
            date = format_date(object.timestamp)
        }
    end
end

osm2pgsql.process_node = add_object_change
osm2pgsql.process_way = add_object_change
osm2pgsql.process_relation = add_object_change

osm2pgsql.process_untagged_node = add_object_change
osm2pgsql.process_untagged_way = add_object_change
osm2pgsql.process_untagged_relation = add_object_change


local function add_deleted_object(object)
    change_table:insert{
        osm_type = object.type,
        osm_id = object.id,
        version = object.version,
        action = 'D',
        date = format_date(object.timestamp)
    }
end

osm2pgsql.process_deleted_node = add_deleted_object
osm2pgsql.process_deleted_way = add_deleted_object
osm2pgsql.process_deleted_relation = add_deleted_object

function osm2pgsql.after_relations()
  -- This callback is called after the last relation has been read from
  -- the input file. As objects are guaranteed to come in order
  -- node/way/relation, file reading is done at that point.
  file_reading_in_progress = false
end
