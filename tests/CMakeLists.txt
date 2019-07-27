add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} ${ARGS})
add_dependencies(check osm2pgsql)

add_library(common-pg STATIC common-pg.cpp common-pg.hpp common-cleanup.cpp common-cleanup.hpp)

set(TESTS
  test-expire-tiles.cpp
  test-hstore-match-only.cpp
  test-middle-flat.cpp
  test-middle-pgsql.cpp
  test-middle-ram.cpp
  test-options-database.cpp
  test-options-parse.cpp
  test-options-projection.cpp
  test-output-multi-line-storage.cpp
  test-output-multi-line.cpp
  test-output-multi-point-multi-table.cpp
  test-output-multi-point.cpp
  test-output-multi-poly-trivial.cpp
  test-output-multi-polygon.cpp
  test-output-multi-tags.cpp
  test-output-pgsql-area.cpp
  test-output-pgsql-schema.cpp
  test-output-pgsql-tablespace.cpp
  test-output-pgsql-validgeom.cpp
  test-output-pgsql-z_order.cpp
  test-output-pgsql.cpp
  test-parse-diff.cpp
  test-parse-extra-args.cpp
  test-parse-xml2.cpp
  test-persistent-node-cache.cpp
  test-wildcard-match.cpp
)

foreach (test ${TESTS})
  get_filename_component(test_name ${test} NAME_WE)
  add_executable(${test_name} ${test})
  target_link_libraries(${test_name} osm2pgsql_lib common-pg)
  add_test(NAME ${test_name} COMMAND ${test_name}
    WORKING_DIRECTORY ${osm2pgsql_SOURCE_DIR})
  add_dependencies(check ${test_name})
  message(STATUS "Added test: ${test_name}...")
  set_tests_properties(${test_name} PROPERTIES TIMEOUT ${TESTING_TIMEOUT})
endforeach()

set(TEST_NODB
 test-expire-tiles
 test-middle-ram
 test-options-database
 test-parse-diff
 test-parse-xml2
 test-wildcard-match
)

foreach (test ${TEST_NODB})
  set_property(TEST ${test} PROPERTY LABELS NoDB)
endforeach()

set_property(TEST test-middle-flat PROPERTY LABELS FlatNodes)
set_property(TEST test-output-pgsql PROPERTY LABELS FlatNodes)

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

set_tests_properties(test-output-pgsql-tablespace
                     PROPERTIES FIXTURES_REQUIRED Tablespace)

if (NOT HAVE_LUA)
  # these tests require LUA support
  set_tests_properties(test-output-multi-poly-trivial PROPERTIES WILL_FAIL on)
  set_tests_properties(test-output-multi-tags PROPERTIES WILL_FAIL on)
endif()

find_package(PythonInterp 3)

if (PYTHONINTERP_FOUND)
  add_test(NAME regression-test-pbf COMMAND ${PYTHON_EXECUTABLE} tests/regression-test.py -f tests/liechtenstein-2013-08-03.osm.pbf -x $<TARGET_FILE:osm2pgsql>
    WORKING_DIRECTORY ${osm2pgsql_SOURCE_DIR})
  set_tests_properties(regression-test-pbf PROPERTIES TIMEOUT ${TESTING_TIMEOUT})
  set_tests_properties(regression-test-pbf
      PROPERTIES ENVIRONMENT "HAVE_LUA=${LUA_FOUND}")
  set_tests_properties(regression-test-pbf
                       PROPERTIES FIXTURES_REQUIRED Tablespace)
  message(STATUS "Added test: regression-test-pbf (needs Python with psycopg2 module)")
else()
  message(WARNING "Can not find python, regression test disabled")
endif()
