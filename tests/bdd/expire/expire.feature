Feature: Test osm2pgsql-expire

    Scenario: Test with invalid options
        When running osm2pgsql-expire with parameters
            | -z18 | -m | abc | {TEST_DATA_DIR}/expire/test-data.osm |
        Then execution fails
        Then the error output contains
            """
            Value for --mode must be 'boundary_only', 'full_area', or 'hybrid'
            """

    Scenario: Test with invalid options
        When running osm2pgsql-expire with parameters
            | -z18 | -m | full_area | -f | foo | {TEST_DATA_DIR}/expire/test-data.osm |
        Then execution fails
        Then the error output contains
            """
            Value for --format must be 'tiles' or 'geojson'
            """

    Scenario: Test expire for various objects on zoom 18 (geojson format)
        When running osm2pgsql-expire with parameters
            | -z18 | -m | full_area | -f | geojson | {TEST_DATA_DIR}/expire/test-data.osm |
        Then execution is successful
        Then the standard output matches contents of expire/test-z18-b0.geojson

    Scenario: Test expire for various objects on zoom 18 (tiles format)
        When running osm2pgsql-expire with parameters
            | -z18 | -m | full_area | -f | tiles | {TEST_DATA_DIR}/expire/test-data.osm |
        Then execution is successful
        Then the standard output matches contents of expire/test-z18-b0.tiles

    Scenario: Test expire for various objects on zoom 18 (geojson format)
        When running osm2pgsql-expire with parameters
            | -z18 | -m | full_area | -f | geojson | -b | 0.5 | {TEST_DATA_DIR}/expire/test-data.osm |
        Then execution is successful
        Then the standard output matches contents of expire/test-z18-b05.geojson

    Scenario: Test expire for various objects on zoom 18 (tiles format)
        When running osm2pgsql-expire with parameters
            | -z18 | -m | full_area | -f | tiles | -b | 0.5 | {TEST_DATA_DIR}/expire/test-data.osm |
        Then execution is successful
        Then the standard output matches contents of expire/test-z18-b05.tiles

