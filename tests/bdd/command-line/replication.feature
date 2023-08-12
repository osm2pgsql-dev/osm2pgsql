Feature: Tests for the osm2pgsql-replication script with property table

    Scenario: Replication can be initialised with an osm file after import
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
        And the replication service at http://example.com/europe/liechtenstein-updates
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init | --osm-file={TEST_DATA_DIR}/liechtenstein-2013-08-03.osm.pbf |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 9999999                                         |
            | replication_timestamp       | 2013-08-03T19:00:02Z                            |


    Scenario: Replication will be initialised from the information of the import file
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 9999999                                         |
            | replication_timestamp       | 2013-08-03T19:00:02Z                            |


    Scenario: Replication cannot be initialised when date information is missing
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
        When running osm2pgsql pgsql with parameters
            | --slim |

        Then running osm2pgsql-replication fails with returncode 1
            | init |
        And the error output contains
            """
            Cannot get timestamp from database.
            """

    Scenario: Replication cannot initialised on non-updatable database
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        When running osm2pgsql pgsql

        Then running osm2pgsql-replication fails with returncode 1
            | init |
        And the error output contains
            """
            Database needs to be imported in --slim mode.
            """

    Scenario: Replication can be initialised for a database in a different schema
        Given the database schema foobar
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
        When running osm2pgsql pgsql with parameters
            | --slim | --middle-schema=foobar |

        And running osm2pgsql-replication
            | init | --middle-schema=foobar |

        Then table foobar.osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 9999999                                         |
            | replication_timestamp       | 2013-08-03T19:00:02Z                            |


    Scenario: Replication initialisation will fail for a database in a different schema
        Given the database schema foobar
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
        When running osm2pgsql pgsql with parameters
            | --slim |

        Then running osm2pgsql-replication fails with returncode 1
            | init | --middle-schema=foobar |
        And the error output contains
            """
            Database needs to be imported in --slim mode.
            """

    Scenario: Replication can be initialised with a fixed date (no previous replication info)
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
        And the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-04T02:00:00Z |
            | 347      | 2020-10-04T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init | --start-at | 2020-10-04T01:30:00Z |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | https://planet.openstreetmap.org/replication/minute |
            | replication_sequence_number | 345                                             |
            | replication_timestamp       | 2020-10-04T01:30:00Z                            |


    Scenario: Replication can be initialised with a fixed date (with previous replication info)
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-04T02:00:00Z |
            | 347      | 2020-10-04T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init | --start-at | 2020-10-04T03:30:00Z |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 347                                             |
            | replication_timestamp       | 2020-10-04T03:30:00Z                            |


    Scenario: Replication can be initialised from database date
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3 t2020-10-04T04:00:01Z
            """
        And the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-04T02:00:00Z |
            | 347      | 2020-10-04T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | https://planet.openstreetmap.org/replication/minute |
            | replication_sequence_number | 345                                             |
            | replication_timestamp       | 2020-10-04T01:00:01Z                            |


    Scenario: Replication can be initialised with a rollback (no previous replication info)
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3 t2020-10-04T02:00:01Z
            """
        And the replication service at https://planet.openstreetmap.org/replication/minute
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-04T02:00:00Z |
            | 347      | 2020-10-04T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init | --start-at | 60 |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | https://planet.openstreetmap.org/replication/minute |
            | replication_sequence_number | 345                                             |
            | replication_timestamp       | 2020-10-04T01:00:01Z                            |


    Scenario: Replication can be initialised with a rollback (with previous replication info)
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 345      | 2020-10-04T01:00:00Z |
            | 346      | 2020-10-04T02:00:00Z |
            | 347      | 2020-10-04T03:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init | --start-at | 60 |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 345                                             |
            | replication_timestamp       | 2013-08-03T14:55:30Z                            |


    Scenario: Replication can be initialised from a different server
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at https://custom.replication
            | sequence | timestamp            |
            | 1345     | 2013-07-01T01:00:00Z |
            | 1346     | 2013-08-01T01:00:00Z |
            | 1347     | 2013-09-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init | --server | https://custom.replication |

        Then table osm2pgsql_properties contains
            | property                    | value                      |
            | replication_base_url        | https://custom.replication |
            | replication_sequence_number | 1346                       |
            | replication_timestamp       | 2013-08-03T12:55:30Z       |


    Scenario: Updates need an initialised replication
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
        And the replication service at https://planet.openstreetmap.org/replication/minute
        When running osm2pgsql pgsql with parameters
            | --slim |

        Then running osm2pgsql-replication fails with returncode 1
            | update |
        And the error output contains
            """
            Updates not set up correctly.
            """

    Scenario: Updates run until the end (exactly one application)
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 9999999  | 2013-08-01T01:00:02Z |
            | 10000000 | 2013-09-01T01:00:00Z |
            | 10000001 | 2013-10-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And running osm2pgsql-replication
            | init |
        And running osm2pgsql-replication
            | update |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 10000001                                        |
            | replication_timestamp       | 2013-10-01T01:00:00Z                            |


    Scenario: Updates run until the end (multiple applications)
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 9999999  | 2013-08-01T01:00:02Z |
            | 10000000 | 2013-09-01T01:00:00Z |
            | 10000001 | 2013-10-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And running osm2pgsql-replication
            | init |
        And running osm2pgsql-replication
            | update | --max-diff-size | 1 |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 10000001                                        |
            | replication_timestamp       | 2013-10-01T01:00:00Z                            |


    Scenario: Updates can run only once
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 9999999  | 2013-08-01T01:00:02Z |
            | 10000000 | 2013-09-01T01:00:00Z |
            | 10000001 | 2013-10-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |
        And running osm2pgsql-replication
            | init |
        And running osm2pgsql-replication
            | update | --once | --max-diff-size | 1 |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 10000000                                        |
            | replication_timestamp       | 2013-09-01T01:00:00Z                            |


    Scenario: Status of an uninitialised database fails
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
        And the replication service at https://planet.openstreetmap.org/replication/minute
        When running osm2pgsql pgsql with parameters
            | --slim |

        Then running osm2pgsql-replication fails with returncode 2
            | status | --json |
        And the standard output contains
            """
            "status": 2
            "error": "Updates not set up correctly.
            """

    Scenario: Status of a freshly initialised database
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 9999999  | 2013-08-01T01:00:02Z |
            | 10000000 | 2013-09-01T01:00:00Z |
            | 10000001 | 2013-10-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | status | --json |
        Then the standard output contains
            """
            "status": 0
            "server": {"base_url": "http://example.com/europe/liechtenstein-updates", "sequence": 10000001, "timestamp": "2013-10-01T01:00:00Z"
            "local": {"sequence": 9999999, "timestamp": "2013-08-03T19:00:02Z"
            """
