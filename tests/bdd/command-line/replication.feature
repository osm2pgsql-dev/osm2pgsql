Feature: Tests for the osm2pgsql-replication script

    Scenario: Replication can be initialised

        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init | --osm-file={TEST_DATA_DIR}/liechtenstein-2013-08-03.osm.pbf |

        Then table planet_osm_replication_status has 1 row

    Scenario: Replication can be initialised in different schema

        Given the database schema foobar
        And the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init |
            | --osm-file={TEST_DATA_DIR}/liechtenstein-2013-08-03.osm.pbf |
            | --middle-schema=foobar |

        Then table foobar.planet_osm_replication_status has 1 row
