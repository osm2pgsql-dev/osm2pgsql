# Add a new test
#
# set_test(name [LABELS labels])
#
# name - Name of test (source file without suffix)
# labels - optional labels for the test
function(set_test test_name)
    cmake_parse_arguments(test_param "" "" "LABELS" ${ARGN})

    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} osm2pgsql_lib catch_main_lib)
    add_test(NAME ${test_name} COMMAND ${test_name})

    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${TESTING_TIMEOUT})

    if (DEFINED test_param_LABELS)
        list(FIND test_param_LABELS Tablespace test_num_labels)
        if (${test_num_labels} EQUAL -1)
            list(APPEND test_param_LABELS "NoTablespace")
        else()
            set_tests_properties(${test_name}
                                 PROPERTIES FIXTURES_REQUIRED Tablespace)
        endif()

        set_tests_properties(${test_name} PROPERTIES LABELS "${test_param_LABELS}")
    else()
        set_tests_properties(${test_name} PROPERTIES LABELS NoTablespace)
    endif()

    message(STATUS "Added test: ${test_name}...")
endfunction()

include_directories(SYSTEM "${CMAKE_CURRENT_SOURCE_DIR}/../contrib/catch2/include")

add_definitions(-DTESTDATA_DIR=\"${osm2pgsql_SOURCE_DIR}/tests/data/\")
add_definitions(-DOSM2PGSQLDATA_DIR=\"${osm2pgsql_SOURCE_DIR}/\")

add_library(catch_main_lib STATIC catch-main.cpp)
target_compile_features(catch_main_lib PUBLIC cxx_std_17)

set_test(test-check-input LABELS NoDB)
set_test(test-db-copy-mgr)
set_test(test-db-copy-thread)
set_test(test-domain-matcher LABELS NoDB)
set_test(test-expire-from-geometry LABELS NoDB)
set_test(test-expire-tiles LABELS NoDB)
set_test(test-flex-indexes LABELS NoDB)
set_test(test-geom-box LABELS NoDB)
set_test(test-geom-collections LABELS NoDB)
set_test(test-geom-linestrings LABELS NoDB)
set_test(test-geom-multilinestrings LABELS NoDB)
set_test(test-geom-multipoints LABELS NoDB)
set_test(test-geom-multipolygons LABELS NoDB)
set_test(test-geom-null LABELS NoDB)
set_test(test-geom-output LABELS NoDB)
set_test(test-geom-points LABELS NoDB)
set_test(test-geom-pole-of-inaccessibility LABELS NoDB)
set_test(test-geom-polygons LABELS NoDB)
set_test(test-geom-transform LABELS NoDB)
set_test(test-hex LABELS NoDB)
set_test(test-json-writer LABELS NoDB)
set_test(test-locator LABELS NoDB)
set_test(test-lua-utils LABELS NoDB)
set_test(test-middle)
set_test(test-node-locations LABELS NoDB)
set_test(test-options-parse LABELS NoDB)
set_test(test-options-projection)
set_test(test-ordered-index LABELS NoDB)
set_test(test-osm-file-parsing LABELS NoDB)
set_test(test-output-flex)
set_test(test-output-flex-multi-input)
set_test(test-output-flex-nodes)
set_test(test-output-flex-relation-combinations)
set_test(test-output-flex-relations)
set_test(test-output-flex-schema)
set_test(test-output-flex-stage2)
set_test(test-output-flex-tablespace LABELS Tablespace)
set_test(test-output-flex-types)
set_test(test-output-flex-uni)
set_test(test-output-flex-update)
set_test(test-output-flex-validgeom)
set_test(test-output-pgsql)
set_test(test-output-pgsql-area)
set_test(test-output-pgsql-hstore-match-only)
set_test(test-output-pgsql-int4)
set_test(test-output-pgsql-schema)
set_test(test-output-pgsql-style-file)
set_test(test-output-pgsql-tablespace LABELS Tablespace)
set_test(test-output-pgsql-validgeom)
set_test(test-output-pgsql-z_order)
set_test(test-params LABELS NoDB)
set_test(test-persistent-cache LABELS NoDB)
set_test(test-pgsql)
set_test(test-pgsql-capabilities)
set_test(test-properties)
set_test(test-reprojection LABELS NoDB)
set_test(test-taginfo LABELS NoDB)
set_test(test-tile LABELS NoDB)
set_test(test-util LABELS NoDB)
set_test(test-wildcard-match LABELS NoDB)
set_test(test-wkb LABELS NoDB)

set_test(test-output-flex-example-configs)
set(FLEX_EXAMPLE_CONFIGS "addresses,attributes,bbox,compatible,data-types,expire,generic,geometries,indexes,places,route-relations,simple,unitable")
# with-schema.lua is not tested because it needs the schema created in the database
set_tests_properties(test-output-flex-example-configs PROPERTIES ENVIRONMENT "EXAMPLE_FILES=${FLEX_EXAMPLE_CONFIGS}")

# Fixture for creating test tablespace under a pg_virtualenv
if (NOT WIN32)
    message(STATUS "Added tablespace fixture...")
    add_test(NAME FixtureTablespaceSetup
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/fixture-tablespace-setup
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME FixtureTablespaceCleanup
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/fixture-tablespace-cleanup
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    set_tests_properties(FixtureTablespaceSetup PROPERTIES FIXTURES_SETUP Tablespace)
    set_tests_properties(FixtureTablespaceCleanup PROPERTIES FIXTURES_CLEANUP Tablespace)
endif()


find_program(BEHAVE_BIN NAMES behave)

if (BEHAVE_BIN)
    foreach(BDD_TEST IN ITEMS command-line flex regression)
        add_test(NAME bdd-${BDD_TEST}
                 COMMAND behave -DBINARY=$<TARGET_FILE:osm2pgsql> ${BDD_TEST}
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bdd)
        message(STATUS "Added test: bdd-${BDD_TEST}")
    endforeach(BDD_TEST)

    set_tests_properties(bdd-regression
                         PROPERTIES FIXTURES_REQUIRED Tablespace)
else()
    message(WARNING "Cannot find behave, BDD tests disabled")
endif()

if (LUA_EXE)
    message(STATUS "Added test: lua-lib")
    add_test(NAME lua-lib COMMAND ${LUA_EXE} lua/tests.lua
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(lua-lib PROPERTIES LABELS NoDB)
else()
    message(WARNING "Can not find Lua interpreter, test 'lua-lib' disabled")
endif()
