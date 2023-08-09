Feature: Tests for the osm2pgsql-replication script without property table

    Background:
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            n35 x77 y45.31
            w4 Thighway=residential Nn34,n35
            """

    Scenario: Replication can be initialised with a osm file
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        And running osm2pgsql-replication
            | init | --osm-file={TEST_DATA_DIR}/liechtenstein-2013-08-03.osm.pbf |

        Then table planet_osm_replication_status contains exactly
            | url                                             | sequence | importdate at time zone 'UTC' |
            | http://example.com/europe/liechtenstein-updates | 9999999  | 2013-08-03 19:00:02           |


    Scenario: Replication cannot be initialised from a osm file without replication info
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        Then running osm2pgsql-replication fails with returncode 1
            | init | --osm-file={TEST_DATA_DIR}/008-ch.osc.gz |
        And the error output contains
            """
            has no usable replication headers
            """


    Scenario: Replication can be initialised in different schema
        Given the database schema foobar
        When running osm2pgsql pgsql with parameters
            | --slim | --middle-schema=foobar |

        And deleting table foobar.osm2pgsql_properties

        And running osm2pgsql-replication
            | init |
            | --osm-file={TEST_DATA_DIR}/liechtenstein-2013-08-03.osm.pbf |
            | --middle-schema=foobar |

        Then table foobar.planet_osm_replication_status contains exactly
            | url                                             | sequence | importdate at time zone 'UTC' |
            | http://example.com/europe/liechtenstein-updates | 9999999  | 2013-08-03 19:00:02           |


    Scenario: Replication must be initialised in the same schema as rest of middle
        Given the database schema foobar
        When running osm2pgsql pgsql with parameters
            | --slim | --middle-schema=foobar |

        And deleting table foobar.osm2pgsql_properties

        Then running osm2pgsql-replication fails with returncode 1
            | init |
            | --osm-file={TEST_DATA_DIR}/liechtenstein-2013-08-03.osm.pbf |
         And the error output contains
            """
            Database needs to be imported in --slim mode.
            """

