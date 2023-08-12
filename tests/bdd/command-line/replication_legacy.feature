Feature: Tests for the osm2pgsql-replication script without property table

    Background:
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            n35 x77 y45.31
            w4 Thighway=residential Nn34,n35
            """

    Scenario: Replication can be initialised with a osm file
        Given the replication service at http://example.com/europe/liechtenstein-updates
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        And running osm2pgsql-replication
            | init | --osm-file={TEST_DATA_DIR}/liechtenstein-2013-08-03.osm.pbf |

        Then table planet_osm_replication_status contains exactly
            | url                                             | sequence | importdate at time zone 'UTC' |
            | http://example.com/europe/liechtenstein-updates | 9999999  | 2013-08-03 19:00:02           |


    Scenario: Replication cannot be initialised from a osm file without replication info
        Given the replication service at http://example.com/europe/liechtenstein-updates
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
        Given the replication service at http://example.com/europe/liechtenstein-updates
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
        Given the replication service at http://example.com/europe/liechtenstein-updates
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

    Scenario: Replication can be initialised with a fixed start date
        Given the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-14T02:00:00Z |
            | 347      | 2020-10-24T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        And running osm2pgsql-replication
            | init | --start-at | 2020-10-22T04:05:06Z |

        Then table planet_osm_replication_status contains exactly
            | url                                                 | sequence | importdate at time zone 'UTC' |
            | https://planet.openstreetmap.org/replication/minute | 346  | 2020-10-22 04:05:06               |


    Scenario: Replication can be initialised from the data in the database
        Given the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-14T02:00:00Z |
            | 347      | 2020-10-24T03:00:00Z |
        And the URL https://www.openstreetmap.org/api/0.6/way/4/1 returns
            """
            {"version":"0.6","generator":"OpenStreetMap server","copyright":"OpenStreetMap and contributors","attribution":"http://www.openstreetmap.org/copyright","license":"http://opendatacommons.org/licenses/odbl/1-0/","elements":[{"type":"way","id":4,"timestamp":"2020-10-15T12:21:53Z","version":1,"changeset":234165,"user":"ewg2","uid":2,"nodes":[34,35],"tags":{"highway":"residential"}}]}
            """
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        And running osm2pgsql-replication
            | init |

        Then table planet_osm_replication_status contains exactly
            | url                                                 | sequence | importdate at time zone 'UTC' |
            | https://planet.openstreetmap.org/replication/minute | 346      | 2020-10-15 09:21:53           |


    Scenario: Replication can be initialised from the data in the database with rollback
        Given the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-14T02:00:00Z |
            | 347      | 2020-10-24T03:00:00Z |
        And the URL https://www.openstreetmap.org/api/0.6/way/4/1 returns
            """
            {"version":"0.6","generator":"OpenStreetMap server","copyright":"OpenStreetMap and contributors","attribution":"http://www.openstreetmap.org/copyright","license":"http://opendatacommons.org/licenses/odbl/1-0/","elements":[{"type":"way","id":4,"timestamp":"2020-10-15T12:21:53Z","version":1,"changeset":234165,"user":"ewg2","uid":2,"nodes":[34,35],"tags":{"highway":"residential"}}]}
            """
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        And running osm2pgsql-replication
            | init | --start-at | 120 |

        Then table planet_osm_replication_status contains exactly
            | url                                                 | sequence | importdate at time zone 'UTC' |
            | https://planet.openstreetmap.org/replication/minute | 346      | 2020-10-15 10:21:53           |


    Scenario: Updates need an initialised replication
        Given the replication service at https://planet.openstreetmap.org/replication/minute
        When running osm2pgsql pgsql with parameters
            | --slim |

        Then running osm2pgsql-replication fails with returncode 1
            | update |
        And the error output contains
            """
            Updates not set up correctly.
            """

    Scenario: Updates run until the end (exactly one application)
        Given the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-14T02:00:00Z |
            | 347      | 2020-10-24T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And running osm2pgsql-replication
            | init | --start-at | 2020-10-04T01:05:06Z |
        And running osm2pgsql-replication
            | update |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | https://planet.openstreetmap.org/replication/minute |
            | replication_sequence_number | 347                                                 |
            | replication_timestamp       | 2020-10-24T03:00:00Z                                |


    Scenario: Updates run until the end (multiple applications)
        Given the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-14T02:00:00Z |
            | 347      | 2020-10-24T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And running osm2pgsql-replication
            | init | --start-at | 2020-10-04T01:05:06Z |
        And running osm2pgsql-replication
            | update | --max-diff-size | 1 |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | https://planet.openstreetmap.org/replication/minute |
            | replication_sequence_number | 347                                                 |
            | replication_timestamp       | 2020-10-24T03:00:00Z                                |


    Scenario: Updates can run only once
        Given the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-14T02:00:00Z |
            | 347      | 2020-10-24T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And running osm2pgsql-replication
            | init | --start-at | 2020-10-04T01:05:06Z |
        And running osm2pgsql-replication
            | update | --max-diff-size | 1 | --once |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | https://planet.openstreetmap.org/replication/minute |
            | replication_sequence_number | 346                                                 |
            | replication_timestamp       | 2020-10-14T02:00:00Z                                |



    Scenario: Status of an uninitialised database fails
        Given the replication service at https://planet.openstreetmap.org/replication/minute
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        Then running osm2pgsql-replication fails with returncode 1
            | status | --json |
        And the standard output contains
            """
            "status": 1
            "error": "Cannot find replication status table
            """

    Scenario: Status of a freshly initialised database
        Given the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-14T02:00:00Z |
            | 347      | 2020-10-24T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And deleting table osm2pgsql_properties

        And running osm2pgsql-replication
            | init | --start-at | 2020-10-22T04:05:06Z |

        And running osm2pgsql-replication
            | status | --json |
        Then the standard output contains
            """
            "status": 0
            "server": {"base_url": "https://planet.openstreetmap.org/replication/minute", "sequence": 347, "timestamp": "2020-10-24T03:00:00Z"
            "local": {"sequence": 346, "timestamp": "2020-10-22T04:05:06Z"
            """
