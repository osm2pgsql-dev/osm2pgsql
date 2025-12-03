Feature: Writing tests for lua styles
    This feature exercises the different steps and matcher
    of the osm2pgsql-test-style script.

    Background:
        Given the OSM data
            """
            n10 Tname=Afeat,access=yes x1 y4
            n11 Tname=B-feat-Ö,access=no x34.5 y-1.5
            """
        And the style file 'flex-config/generic.lua'
        When running osm2pgsql flex

    Scenario: Fields can be matched case-insensitive
        Then table points contains
            | node_id | tags->'name'!i |
            | 10      | AFEAT          |
            | 11      | b-feat-ö       |
        And table points doesn't contain
            | node_id | tags->'name' |
            | 10      | AFEAT        |


    Scenario: Fields can be match with a regular expression
        Then table points contains
            | node_id | tags!re |
            | 10      | .*access.*   |
            | 11      | .*-[a-z]+-.* |


    Scenario: Fields can be matched as a substring
        Then table points contains
            | node_id | tags->'name'!substr |
            | 10      | feat                |
            | 11      | feat                |
        And table points doesn't contain
            | node_id | tags->'name' |
            | 10      | ?            |


    Scenario: Fields can be matched as a json expression
        Then table points contains
            | node_id | tags!json |
            | 10      | {"name": "Afeat", "access": "yes"} |
        Then table points contains
            | node_id | tags!json |
            | 10      | {"access": "yes", "name": "Afeat"} |


    Scenario: Floating fields can be matched with different precision
        Then table points contains
            | node_id | ST_X(geom)!~0.5 |
            | 11      | 3840522.0 |
        Then table points doesn't contain
            | node_id | ST_X(geom)!~0.1 |
            | 11      | 3840522.0 |
        Then table points contains
            | node_id | ST_X(geom)!~1% |
            | 11      | 3840000.0 |
        Then table points doesn't contain
            | node_id | ST_X(geom)!~0.01% |
            | 11      | 3840000.0 |


    Scenario: Fields can be formatted arbitrarily for comparison
        Then table points contains
            | node_id!:03d | ST_X(geom)!:.1f |
            | 010          | 111319.5             |
