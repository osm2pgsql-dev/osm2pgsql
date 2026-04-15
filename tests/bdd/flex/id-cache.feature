Feature: Id cache

    Background:
        Given the 0.1 grid
            |    | 10 | 11 | 12 |
            | 14 | 15 |    | 16 |
        And the lua style
            """
            local barriers = osm2pgsql.define_table({
                name = 'barriers',
                ids = { type = 'node', id_column = 'node_id', cache = true },
                columns = {
                    { column = 'btype', type = 'text', not_null = true },
                    { column = 'geom', type = 'point', projection = 4326, not_null = true },
                }
            })
            local highways = osm2pgsql.define_table({
                name = 'highways',
                ids = { type = 'way', id_column = 'way_id' },
                columns = {
                    { column = 'htype', type = 'text', not_null = true },
                    { column = 'geom', type = 'linestring', projection = 4326, not_null = true },
                }
            })
            local b_on_h = osm2pgsql.define_table({
                name = 'b_on_h',
                ids = { type = 'way', id_column = 'way_id' },
                columns = {
                    { column = 'node_id', type = 'int8', not_null = true },
                    { column = 'htype', type = 'text', not_null = true },
                    { column = 'hgeom', type = 'linestring', projection = 4326, not_null = true },
                    { column = 'bgeom', type = 'point', projection = 4326, not_null = true },
                }
            })

            function osm2pgsql.process_node(object)
                local t = object.tags.barrier
                if t then
                    barriers:insert({
                        btype = t,
                        geom = object:as_point(),
                    })
                end
            end

            function osm2pgsql.process_way(object)
                local t = object.tags.highway
                if t then
                    highways:insert({
                        htype = t,
                        geom = object:as_linestring(),
                    })
                    local bidx = barriers:in_id_cache(object.nodes)
                    for _, idx in ipairs(bidx) do
                        b_on_h:insert({
                            node_id = object.nodes[idx],
                            htype = t,
                            hgeom = object:as_linestring(),
                            bgeom = object:as_point(idx),
                        })
                    end
                end
            end
            """

    Scenario: Id cache works with simple import
        Given the OSM data
            """
            n10 v1 dV Tbarrier=gate
            n16 v1 dV Tbarrier=lift_gate
            w20 v1 dV Thighway=residential Nn10,n11,n12,n16
            w21 v1 dV Thighway=residential Nn14,n15,n10
            """
        When running osm2pgsql flex
        Then table barriers contains exactly
            | node_id | btype     | geom!geo |
            | 10      | gate      | 10       |
            | 16      | lift_gate | 16       |
        Then table highways contains exactly
            | way_id  | htype       | geom!geo    |
            | 20      | residential | 10,11,12,16 |
            | 21      | residential | 14,15,10    |
        Then table b_on_h contains exactly
            | way_id  | node_id | htype       | bgeom!geo | hgeom!geo   |
            | 20      | 10      | residential | 10        | 10,11,12,16 |
            | 20      | 16      | residential | 16        | 10,11,12,16 |
            | 21      | 10      | residential | 10        | 14,15,10    |

    Scenario: Id cache works with updates
        Given the OSM data
            """
            n10 v1 dV Tbarrier=gate
            n16 v1 dV Tbarrier=lift_gate
            w20 v1 dV Thighway=residential Nn10,n11,n12,n16
            w21 v1 dV Thighway=residential Nn14,n15,n10
            """
        When running osm2pgsql flex with parameters
            | --slim |
        Then table barriers contains exactly
            | node_id | btype     | geom!geo |
            | 10      | gate      | 10       |
            | 16      | lift_gate | 16       |
        Then table highways contains exactly
            | way_id  | htype       | geom!geo    |
            | 20      | residential | 10,11,12,16 |
            | 21      | residential | 14,15,10    |
        Then table b_on_h contains exactly
            | way_id  | node_id | htype       | bgeom!geo | hgeom!geo   |
            | 20      | 10      | residential | 10        | 10,11,12,16 |
            | 20      | 16      | residential | 16        | 10,11,12,16 |
            | 21      | 10      | residential | 10        | 14,15,10    |

        Given the OSM data
            """
            n10 v2 dV Tno=barrier
            n11 v2 dV Tbarrier=gate
            """
        When running osm2pgsql flex with parameters
            | --slim | -a |
        Then table barriers contains exactly
            | node_id | btype     | geom!geo |
            | 11      | gate      | 11       |
            | 16      | lift_gate | 16       |
        Then table highways contains exactly
            | way_id  | htype       | geom!geo    |
            | 20      | residential | 10,11,12,16 |
            | 21      | residential | 14,15,10    |
        Then table b_on_h contains exactly
            | way_id  | node_id | htype       | bgeom!geo | hgeom!geo   |
            | 20      | 11      | residential | 11        | 10,11,12,16 |
            | 20      | 16      | residential | 16        | 10,11,12,16 |

