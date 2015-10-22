add_library(common-pg STATIC common-pg.cpp common-pg.hpp common-cleanup.cpp common-cleanup.hpp)
add_library(middle-tests STATIC middle-tests.cpp middle-tests.hpp)

set(TESTS
  test-expire-tiles.cpp
  test-hstore-match-only.cpp
  test-middle-flat.cpp
  test-middle-pgsql.cpp
  test-middle-ram.cpp
  test-options-database.cpp
  test-options-parse.cpp
  test-output-multi-line-storage.cpp
  test-output-multi-line.cpp
  test-output-multi-point-multi-table.cpp
  test-output-multi-point.cpp
  test-output-multi-poly-trivial.cpp
  test-output-multi-polygon.cpp
  test-output-multi-tags.cpp
  test-output-pgsql-schema.cpp
  test-output-pgsql-tablespace.cpp
  test-output-pgsql-z_order.cpp
  test-output-pgsql.cpp
  test-parse-xml2.cpp
  test-pgsql-escape.cpp
)

foreach (test ${TESTS})
  get_filename_component(test_name ${test} NAME_WE)
  add_executable(${test_name} ${test})
  target_link_libraries(${test_name} osm2pgsql_lib common-pg middle-tests)
  add_test(NAME ${test_name} COMMAND ${test_name}
    WORKING_DIRECTORY ${osm2pgsql_SOURCE_DIR})
  message(STATUS "Added test: ${test_name}...")
  set_tests_properties(${test_name} PROPERTIES TIMEOUT ${TESTING_TIMEOUT})
endforeach(test)

set(TEST_NODB
 test-expire-tiles
 test-middle-ram
 test-options-database
 test-options-parse
 test-parse-xml2
 test-pgsql-escape
)

foreach (test ${TEST_NODB})
  set_property(TEST ${test} PROPERTY LABELS NoDB)
endforeach()

set_property(TEST test-middle-flat PROPERTY LABELS FlatNodes)
set_property(TEST test-output-pgsql PROPERTY LABELS FlatNodes)

if (TESTS_VALGRIND)
  set_property(TEST valgrind-test-middle-flat PROPERTY LABELS FlatNodes)
  set_property(TEST valgrind-test-output-pgsql PROPERTY LABELS FlatNodes)
endif()

find_package(PythonInterp)

if (PYTHONINTERP_FOUND)
  add_test(NAME regression-test-pbf COMMAND ${PYTHON_EXECUTABLE} tests/regression-test.py -f tests/liechtenstein-2013-08-03.osm.pbf
    WORKING_DIRECTORY ${osm2pgsql_SOURCE_DIR})
  set_tests_properties(regression-test-pbf PROPERTIES TIMEOUT ${TESTING_TIMEOUT})
  message(STATUS "Added test: regression-test-pbf (needs Python with psycopg2 module)")
else()
  message(WARNING "Can not find python, regression test disabled")
endif()