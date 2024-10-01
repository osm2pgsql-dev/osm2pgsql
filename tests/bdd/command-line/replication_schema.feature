Feature: Tests for the osm2pgsql-replication script with schemas

    Scenario: Replication updates work on database with schema
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the database schema foobar
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 9999999  | 2013-08-01T01:00:02Z |
            | 10000000 | 2013-09-01T01:00:00Z |
            | 10000001 | 2013-10-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim | --schema | foobar |
        And running osm2pgsql-replication
            | init | --schema | foobar |
        And running osm2pgsql-replication
            | update | --schema | foobar |

        Then table foobar.osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 10000001                                        |
            | replication_timestamp       | 2013-10-01T01:00:00Z                            |


    Scenario: Replication updates work on database with different middle schema
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the database schema foobar
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 9999999  | 2013-08-01T01:00:02Z |
            | 10000000 | 2013-09-01T01:00:00Z |
            | 10000001 | 2013-10-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim | --middle-schema | foobar |
        And running osm2pgsql-replication
            | init | --middle-schema | foobar |
        And running osm2pgsql-replication
            | update | --middle-schema | foobar |

        Then table foobar.osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 10000001                                        |
            | replication_timestamp       | 2013-10-01T01:00:00Z                            |


    Scenario: Replication updates work on database with middle schema different from schema
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the database schema foobar
        And the database schema baz
        And the replication service at http://example.com/europe/liechtenstein-updates
            | sequence | timestamp            |
            | 9999999  | 2013-08-01T01:00:02Z |
            | 10000000 | 2013-09-01T01:00:00Z |
            | 10000001 | 2013-10-01T01:00:00Z |
        When running osm2pgsql pgsql with parameters
            | --slim | --middle-schema | foobar | --schema | baz |
        And running osm2pgsql-replication
            | init | --middle-schema | foobar | --schema | baz |
        And running osm2pgsql-replication
            | update | --middle-schema | foobar | --schema | baz |

        Then table foobar.osm2pgsql_properties contains
            | property                    | value                                           |
            | replication_base_url        | http://example.com/europe/liechtenstein-updates |
            | replication_sequence_number | 10000001                                        |
            | replication_timestamp       | 2013-10-01T01:00:00Z                            |

