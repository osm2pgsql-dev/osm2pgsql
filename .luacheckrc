--
--  Configuration for luacheck
--
--  To check Lua files call like this:
--  luacheck flex-config/*.lua flex-config/gen/*.lua tests/data/*.lua tests/lua/tests.lua
--

unused_args = false

stds.osm2pgsql = {
    read_globals = {
        osm2pgsql = {
            fields = {
                process_node = {
                    read_only = false
                },
                process_way = {
                    read_only = false
                },
                process_relation = {
                    read_only = false
                },
                select_relation_members = {
                    read_only = false
                },
                process_gen = {
                    read_only = false
                },
            },
            other_fields = true,
        }
    }
}

std = 'min+osm2pgsql'

files['tests/lua/tests.lua'].globals = { 'osm2pgsql' }

