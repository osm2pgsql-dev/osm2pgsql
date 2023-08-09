Feature: Tests for the osm2pgsql-replication script with property table

    Scenario: Replication can be initialised with a osm file after import
        Given the OSM data
            """
            n34 Tamenity=restaurant x77 y45.3
            """
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
        When running osm2pgsql pgsql with parameters
            | --slim |

        And running osm2pgsql-replication
            | init |

        Then table osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 9999999                                         |
            | replication_timestamp       | 2013-08-03T19:00:02Z                            |


    Scenario: Replication cannot be initialsed when date information is missing
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
        When running osm2pgsql pgsql with parameters
            | --slim | --middle-schema=foobar |

        And running osm2pgsql-replication
            | init | --middle-schema=foobar |

        Then table foobar.osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 9999999                                         |
            | replication_timestamp       | 2013-08-03T19:00:02Z                            |


    Scenario: Replication initialiasion will fail for a database in a different schema
        Given the database schema foobar
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        When running osm2pgsql pgsql with parameters
            | --slim |

        Then running osm2pgsql-replication fails with returncode 1
            | init | --middle-schema=foobar |
        And the error output contains
            """
            Database needs to be imported in --slim mode.
            """

