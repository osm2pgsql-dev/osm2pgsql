Feature: Expire changed-geometry endpoints into a table

    Background:
        Given the lua style
            """
            local eo = osm2pgsql.define_expire_output({
                endpoint_table = 'changed_endpoints'
            })
            local t = osm2pgsql.define_way_table('ways', {
                { column = 'geom', type = 'linestring', expire = eo },
                { column = 'tags', type = 'jsonb' }
            })

            function osm2pgsql.process_way(object)
                if object.tags.highway then
                    t:insert({
                        tags = object.tags,
                        geom = object:as_linestring()
                    })
                end
            end
            """

    Scenario: Endpoints of a changed way are written to the endpoint table
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        When running osm2pgsql flex with parameters
            | --slim | -c |
        # The initial import does not run expiry, so no endpoints are recorded.
        Then table changed_endpoints has 0 rows

        # Use ids far above anything in the extract
        Given the OSM data
            """
            n8000000001 v1 dV x10.0 y47.0
            n8000000002 v1 dV x10.001 y47.0
            w8000000003 v1 dV Thighway=residential Nn8000000001,n8000000002
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        # The added way is isolated, so it contributes its two distinct endpoints.
        Then table changed_endpoints has 2 rows
