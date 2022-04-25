Feature: Basic tests of the gazetter output

    Background:
        Given the input file 'liechtenstein-2013-08-03.osm.pbf'
        And the style file 'gazetteer-test.style'

    Scenario:
        When running osm2pgsql gazetteer with parameters
            | --slim|

        Then table place has 2836 rows
        And table place has 759 rows with condition
            """
            osm_type = 'N'
            """
        And table place has 2059 rows with condition
            """
            osm_type = 'W'
            """
        And table place has 18 rows with condition
            """
            osm_type = 'R'
            """
        And table place has 199 rows with condition
            """
            address ? 'housenumber'
            """
        And table place has 319 rows with condition
            """
            address is not null
            """

        Given the input file '000466354.osc.gz'
        When running osm2pgsql gazetteer with parameters
            | -a |
            | --slim|


        Then table place has 2877 rows
        And table place has 764 rows with condition
            """
            osm_type = 'N'
            """
        And table place has 2095 rows with condition
            """
            osm_type = 'W'
            """
        And table place has 18 rows with condition
            """
            osm_type = 'R'
            """
        And table place has 199 rows with condition
            """
            address ? 'housenumber'
            """
        And table place has 319 rows with condition
            """
            address is not null
            """

