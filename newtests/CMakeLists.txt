set (TESTS
    test-db-copy-thread.cpp
    test-db-copy-mgr.cpp
    test-middle.cpp
    test-options-parse.cpp
    test-output-gazetteer.cpp
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


