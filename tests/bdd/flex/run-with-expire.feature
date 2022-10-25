Feature: Expire into table

    Background:
        Given the lua style
            """
            osm2pgsql.define_tileset({
                name = 'tiles',
                table = 'tiles',
                maxzoom = 12
            })
            local t = osm2pgsql.define_node_table('nodes', {
                { column = 'geom', type = 'point', expire = { { tileset = 'tiles' } }},
                { column = 'tags', type = 'jsonb' }
            })

            function osm2pgsql.process_node(object)
                if object.tags then
                    t:insert({
                        tags = object.tags,
                        geom = object:as_point()
                    })
                end
            end
            """

    Scenario: Expire into table in append mode
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        When running osm2pgsql flex with parameters
            | --slim | -c |
        Then table nodes has 1562 rows
        And table tiles has 0 rows

        Given the OSM data
            """
            n27 v1 dV x10 y10 Tamenity=restaurant
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table nodes has 1563 rows
        And table tiles has 1 rows

