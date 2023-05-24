Feature: Tests for including extra attributes

    Scenario: Importing data without extra attributes
        Given the grid
            | 11 | 12 |
            | 10 |    |
        And the OSM data
            """
            w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest Thighway=primary Nn10,n11,n12
            """
        When running osm2pgsql pgsql with parameters
            | --slim | -j |

        Then table planet_osm_roads contains
            | osm_id | tags->'highway' | tags->'osm_version' | tags->'osm_changeset' |
            | 20     | primary         | NULL                | NULL                  |

        Given the grid
            |    |    |
            |    | 10 |
        When running osm2pgsql pgsql with parameters
            | --slim | -j | --append |

        Then table planet_osm_roads contains
            |  osm_id | tags->'highway' | tags->'osm_version' | tags->'osm_changeset' |
            |  20     | primary         | NULL                | NULL                  |


    Scenario: Importing data with extra attributes
        Given the grid
            | 11 | 12 |
            | 10 |    |
        And the OSM data
            """
            w20 v1 dV c31 t2020-01-12T12:34:56Z i17 utest Thighway=primary Nn10,n11,n12
            """
        When running osm2pgsql pgsql with parameters
            | --slim | -j | -x |

        Then table planet_osm_roads contains
            |  osm_id | tags->'highway' | tags->'osm_version' | tags->'osm_changeset' |
            |  20     | primary         | 1                   | 31                    |

        Given the grid
            |    |    |
            |    | 10 |
        When running osm2pgsql pgsql with parameters
            | --slim | -j | --append | -x |

        Then table planet_osm_roads contains
            |  osm_id | tags->'highway' | tags->'osm_version' | tags->'osm_changeset' |
            |  20     | primary         | 1                   | 31                    |

