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

    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${TESTING_TIMEOUT}
                                                 ENVIRONMENT SRCPATH=${CMAKE_CURRENT_SOURCE_DIR})

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

set_test(test-db-copy-thread)
set_test(test-db-copy-mgr)
set_test(test-domain-matcher LABELS NoDB)
set_test(test-expire-tiles LABELS NoDB)
set_test(test-middle)
set_test(test-options-database LABELS NoDB)
set_test(test-options-parse LABELS NoDB)
set_test(test-options-projection)
set_test(test-output-gazetteer)
set_test(test-output-multi-line)
set_test(test-output-multi-point)
set_test(test-output-multi-point-multi-table)
set_test(test-output-multi-polygon)
set_test(test-output-pgsql)
set_test(test-output-pgsql-area)
set_test(test-output-pgsql-hstore-match-only)
set_test(test-output-pgsql-int4)
set_test(test-output-pgsql-schema)
set_test(test-output-pgsql-tablespace LABELS Tablespace)
set_test(test-output-pgsql-validgeom)
set_test(test-output-pgsql-z_order)
set_test(test-parse-osmium LABELS NoDB)
set_test(test-persistent-cache LABELS NoDB)
set_test(test-pgsql)
set_test(test-ram-cache LABELS NoDB)
set_test(test-reprojection LABELS NoDB)
set_test(test-taginfo LABELS NoDB)
set_test(test-util LABELS NoDB)
set_test(test-wildcard-match LABELS NoDB)

# these tests require LUA support
if (HAVE_LUA)
    set_test(test-output-multi-line-storage)
    set_test(test-output-multi-poly-trivial)
    set_test(test-output-multi-tags)
    set_test(test-output-flex)
    set_test(test-output-flex-area)
    set_test(test-output-flex-attr)
    set_test(test-output-flex-bbox)
    set_test(test-output-flex-invalid-geom)
    set_test(test-output-flex-line)
    set_test(test-output-flex-lua-fail)
    set_test(test-output-flex-nodes)
    set_test(test-output-flex-nogeom)
    set_test(test-output-flex-uni)
    set_test(test-output-flex-relations)
    set_test(test-output-flex-relation-changes)
    set_test(test-output-flex-relation-combinations)
    set_test(test-output-flex-schema)
    set_test(test-output-flex-stage2)
    set_test(test-output-flex-tablespace LABELS Tablespace)
    set_test(test-output-flex-types)
    set_test(test-output-flex-update)
    set_test(test-output-flex-validgeom)
    set_test(test-output-flex-way-add)
    set_test(test-output-flex-way-change)
    set_test(test-output-flex-way-del)
    set_test(test-output-flex-way-relation-add)
    set_test(test-output-flex-way-relation-del)

    set_test(test-output-flex-example-configs)
    set(FLEX_EXAMPLE_CONFIGS "compatible,data-types,generic,geometries,route-relations,simple,unitable")
    # places.lua not tested because it needs dkjson package
    set_tests_properties(test-output-flex-example-configs PROPERTIES ENVIRONMENT "EXAMPLE_FILES=${FLEX_EXAMPLE_CONFIGS}")
endif()


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


find_package(PythonInterp 3)

if (PYTHONINTERP_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/regression-test.py.in
                   ${CMAKE_CURRENT_BINARY_DIR}/regression-test.py)

    add_test(NAME regression-test-pbf
             COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/regression-test.py -v)
    set_tests_properties(regression-test-pbf PROPERTIES TIMEOUT ${TESTING_TIMEOUT})
    set_tests_properties(regression-test-pbf
                         PROPERTIES FIXTURES_REQUIRED Tablespace)
    message(STATUS "Added test: regression-test-pbf (needs Python with psycopg2 module)")
else()
    message(WARNING "Can not find python, regression test disabled")
endif()

if (HAVE_LUA)
    if (LUA_EXE)
        message(STATUS "Added test: lua-lib")
        add_test(NAME lua-lib COMMAND ${LUA_EXE} lua/tests.lua
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(lua-lib PROPERTIES LABELS NoDB)
    else()
        message(WARNING "Can not find Lua interpreter, test 'lua-lib' disabled")
    endif()
endif()
