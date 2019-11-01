set (TESTS
    test-db-copy-thread.cpp
    test-db-copy-mgr.cpp
    test-middle.cpp
    test-options-database.cpp
    test-options-parse.cpp
    test-options-projection.cpp
    test-output-gazetteer.cpp
    test-output-multi-line.cpp
    test-output-multi-line-storage.cpp
    test-output-multi-point.cpp
    test-output-multi-poly-trivial.cpp
    test-output-multi-polygon.cpp
    test-output-multi-tags.cpp
    test-output-pgsql.cpp
    test-output-pgsql-area.cpp
    test-output-pgsql-int4.cpp
    test-output-pgsql-schema.cpp
    test-output-pgsql-tablespace.cpp
    test-output-pgsql-validgeom.cpp
    test-output-pgsql-z_order.cpp
    test-parse-osmium.cpp
    test-persistent-cache.cpp
    test-ram-cache.cpp
    test-wildcard-match.cpp
)

foreach (test ${TESTS})
  get_filename_component(test_name ${test} NAME_WE)
  add_executable(${test_name} ${test} catch-main.cpp)
  target_link_libraries(${test_name} osm2pgsql_lib)
  add_test(NAME ${test_name} COMMAND ${test_name}
    WORKING_DIRECTORY ${osm2pgsql_SOURCE_DIR})
  message(STATUS "Added test: ${test_name}...")
  set_tests_properties(${test_name} PROPERTIES TIMEOUT ${TESTING_TIMEOUT})
endforeach()

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

