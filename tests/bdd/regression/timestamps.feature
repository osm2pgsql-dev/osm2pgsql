Feature: Timestamps in properties table should reflect timestamps in input file

    Scenario: Create database with timestamps
        Given the OSM data
            """
            n10 t2020-01-02T03:04:05Z x10 y10
            n11 t2020-01-02T03:04:05Z x10 y11
            w20 t2020-01-02T03:04:06Z Thighway=primary Nn10,n11
            """
        When running osm2pgsql pgsql

        Then table osm2pgsql_properties has 7 rows
        And table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | false                |
            | import_timestamp  | 2020-01-02T03:04:06Z |
            | current_timestamp | 2020-01-02T03:04:06Z |

    Scenario: Create database without timestamps
        Given the OSM data
            """
            n10 x10 y10
            n11 x10 y11
            w20 Thighway=primary Nn10,n11
            """
        When running osm2pgsql pgsql

        Then table osm2pgsql_properties has 5 rows
        Then table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | false                |

    Scenario: Create and update database with timestamps
        Given the OSM data
            """
            n10 v1 t2020-01-02T03:04:05Z x10 y10
            n11 v1 t2020-01-02T03:04:05Z x10 y11
            w20 v1 t2020-01-02T03:04:06Z Thighway=primary Nn10,n11
            """

        When running osm2pgsql pgsql with parameters
            | --create | --slim |

        Then table osm2pgsql_properties has 7 rows
        And table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | true                 |
            | import_timestamp  | 2020-01-02T03:04:06Z |
            | current_timestamp | 2020-01-02T03:04:06Z |

        Given the OSM data
            """
            n11 v2 t2020-01-02T03:06:05Z x10 y12
            w20 v2 t2020-01-02T03:05:06Z Thighway=secondary Nn10,n11
            """

        When running osm2pgsql pgsql with parameters
            | --append | --slim |

        Then table osm2pgsql_properties has 7 rows
        And table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | true                 |
            | import_timestamp  | 2020-01-02T03:04:06Z |
            | current_timestamp | 2020-01-02T03:06:05Z |

    Scenario: Create database with timestamps and update without timestamps
        Given the OSM data
            """
            n10 v1 t2020-01-02T03:04:05Z x10 y10
            n11 v1 t2020-01-02T03:04:05Z x10 y11
            w20 v1 t2020-01-02T03:04:06Z Thighway=primary Nn10,n11
            """

        When running osm2pgsql pgsql with parameters
            | --create | --slim |

        Then table osm2pgsql_properties has 7 rows
        And table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | true                 |
            | import_timestamp  | 2020-01-02T03:04:06Z |
            | current_timestamp | 2020-01-02T03:04:06Z |

        Given the OSM data
            """
            n11 v2 x10 y12
            w20 v2 Thighway=secondary Nn10,n11
            """

        When running osm2pgsql pgsql with parameters
            | --append | --slim |

        Then table osm2pgsql_properties has 7 rows
        And table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | true                 |
            | import_timestamp  | 2020-01-02T03:04:06Z |
            | current_timestamp | 2020-01-02T03:04:06Z |

    Scenario: Create database without timestamps and update with timestamps
        Given the OSM data
            """
            n10 v1 x10 y10
            n11 v1 x10 y11
            w20 v1 Thighway=primary Nn10,n11
            """

        When running osm2pgsql pgsql with parameters
            | --create | --slim |

        Then table osm2pgsql_properties has 5 rows
        And table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | true                 |

        Given the OSM data
            """
            n11 v2 t2020-01-02T03:06:05Z x10 y12
            w20 v2 t2020-01-02T03:05:06Z Thighway=secondary Nn10,n11
            """

        When running osm2pgsql pgsql with parameters
            | --append | --slim |

        Then table osm2pgsql_properties has 6 rows
        And table osm2pgsql_properties contains
            | property          | value                |
            | attributes        | false                |
            | flat_node_file    |                      |
            | prefix            | planet_osm           |
            | updatable         | true                 |
            | current_timestamp | 2020-01-02T03:06:05Z |

