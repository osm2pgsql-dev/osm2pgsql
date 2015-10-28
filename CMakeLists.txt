SET(PACKAGE osm2pgsql)
SET(PACKAGE_NAME osm2pgsql)
SET(PACKAGE_VERSION 0.89.0-dev)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7)

PROJECT(osm2pgsql)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

SET(DATA_DIR \".\")

OPTION(BUILD_TESTS "Build test suite" OFF)

if (NOT TESTING_TIMEOUT)
  set(TESTING_TIMEOUT 1200)
endif()

if (NOT WIN32 AND NOT APPLE)
  # No need for this path, just a workaround to make cmake check pass on all systems
  set(PostgreSQL_TYPE_INCLUDE_DIR /usr/include)
endif()

# Just in case user installed RPMs from http://yum.postgresql.org/
list(APPEND PostgreSQL_ADDITIONAL_SEARCH_PATHS /usr/pgsql-9.3 /usr/pgsql-9.4)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed, please use a separate build directory like `mkdir build && cd build && cmake ..`")
endif()

message(STATUS "Building osm2pgsql ${PACKAGE_VERSION}")

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if ( MSVC )
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -wd4996)
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /ignore:4099 /STACK:30000000")
else()
  add_definitions(-Wall)
  add_definitions(-std=c++11)
  add_definitions(-DBOOST_TEST_DYN_LINK)
endif()

option(EXTERNAL_LIBOSMIUM "Do not use the bundled libosmium" OFF)

#############################################################
# Detect available headers and set global compiler options
#############################################################

INCLUDE (CheckIncludeFiles)
INCLUDE (CheckFunctionExists)
INCLUDE (CheckTypeSize)

ADD_DEFINITIONS( -DOSM2PGSQL_DATADIR=${DATA_DIR} )
ADD_DEFINITIONS( -DFIXED_POINT )

CHECK_INCLUDE_FILES (termios.h HAVE_TERMIOS_H)
CHECK_INCLUDE_FILES (libgen.h HAVE_LIBGEN_H)
CHECK_INCLUDE_FILES (unistd.h HAVE_UNISTD_H)
CHECK_INCLUDE_FILES (sys/wait.h HAVE_SYS_WAIT_H)
CHECK_INCLUDE_FILES (sys/time.h HAVE_SYS_TIME_H)
CHECK_INCLUDE_FILES (sys/mman.h HAVE_MMAP)

if (WIN32)
  set(HAVE_LIBGEN_H FALSE)
endif()

CHECK_FUNCTION_EXISTS(lseek64 HAVE_LSEEK64)
CHECK_FUNCTION_EXISTS(posix_fallocate HAVE_POSIX_FALLOCATE)
CHECK_FUNCTION_EXISTS(posix_fadvise HAVE_POSIX_FADVISE)
CHECK_FUNCTION_EXISTS(sync_file_range HAVE_SYNC_FILE_RANGE)
CHECK_FUNCTION_EXISTS(fork HAVE_FORK)

CHECK_TYPE_SIZE("off_t" SIZEOF_OFF_T)

if (NOT WIN32 AND NOT APPLE AND NOT HAVE_LSEEK64 AND NOT SIZEOF_OFF_T EQUAL 8)
  message(FATAL_ERROR "Flat nodes cache requires a 64 bit capable seek")
endif()

#############################################################
# Find necessary libraries
#############################################################

if (NOT EXTERNAL_LIBOSMIUM)
  set(OSMIUM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/libosmium")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(Osmium REQUIRED COMPONENTS io geos proj)
include_directories(SYSTEM ${OSMIUM_INCLUDE_DIRS})

find_package(Lua)
if (LUA_FOUND)
  include_directories(${LUA_INCLUDE_DIR})
  set(HAVE_LUA 1)
else()
  message(STATUS "lua libraries not found. You will NOT be able to use lua scripts for tag transform.")
endif()

if (MSVC)
  # Boost thread needs extra libraries
  set(BOOST_EXTRA date_time chrono)
endif()

if (NOT Boost_ADDITIONAL_VERSIONS)
  set(Boost_ADDITIONAL_VERSIONS "1.55;1.56;1.57;1.58;1.59;1.60;1.61")
endif()

# first try to find the version
find_package(Boost 1.50 REQUIRED COMPONENTS system filesystem thread ${BOOST_EXTRA})
include_directories(${Boost_INCLUDE_DIR})

find_package(PostgreSQL REQUIRED)
include_directories(${PostgreSQL_INCLUDE_DIRS})

find_package(Threads)

############### Libraries are found now ########################


set (LIBS ${Boost_LIBRARIES} ${PostgreSQL_LIBRARY} ${OSMIUM_LIBRARIES})

if (LUA_FOUND)
  list(APPEND LIBS ${LUA_LIBRARIES})
endif()

if (WIN32)
  list(APPEND LIBS ws2_32)
  if (MSVC)
    find_path(GETOPT_INCLUDE_DIR getopt.h)
    find_library(GETOPT_LIBRARY NAMES wingetopt getopt )
    if (GETOPT_INCLUDE_DIR AND GETOPT_LIBRARY)
      include_directories(${GETOPT_INCLUDE_DIR})
      list(APPEND LIBS ${GETOPT_LIBRARY})
    else()
      message(ERROR "Can not find getopt library for Windows. Please get it from https://github.com/alex85k/wingetopt or alternative source.")
    endif()
  endif()
endif()

message("Libraries used to build: " ${LIBS})

list(APPEND LIBS ${CMAKE_DL_LIBS})

if (CMAKE_SYSTEM_NAME STREQUAL Linux)
  check_library_exists(rt clock_gettime "time.h" HAVE_CLOCK_GETTIME_IN_RT)
  if (HAVE_CLOCK_GETTIME_IN_RT)
    list(APPEND LIBS rt)
  endif()
endif ()

message("Active compiler flags:" ${CMAKE_CXX_FLAGS})

#############################################################
# Build the library and executable file
#############################################################

set(HAVE_PTHREAD "${CMAKE_USE_PTHREADS_INIT}")
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

if (NOT HAVE_UNISTD_H AND NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/unistd.h)
   file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/unistd.h "// empty header\n")
endif()

set(osm2pgsql_lib_SOURCES
  expire-tiles.cpp
  geometry-builder.cpp
  geometry-processor.cpp
  id-tracker.cpp
  middle-pgsql.cpp
  middle-ram.cpp
  middle.cpp
  node-persistent-cache.cpp
  node-ram-cache.cpp
  options.cpp
  osmdata.cpp
  output-gazetteer.cpp
  output-multi.cpp
  output-null.cpp
  output-pgsql.cpp
  output.cpp
  parse-o5m.cpp
  parse-osmium.cpp
  parse.cpp
  pgsql.cpp
  processor-line.cpp
  processor-point.cpp
  processor-polygon.cpp
  reprojection.cpp
  sprompt.cpp
  table.cpp
  taginfo.cpp
  tagtransform.cpp
  util.cpp
  wildcmp.cpp
  expire-tiles.hpp
  geometry-builder.hpp
  geometry-processor.hpp
  id-tracker.hpp
  middle-pgsql.hpp
  middle-ram.hpp
  middle.hpp
  node-persistent-cache.hpp
  node-ram-cache.hpp
  options.hpp
  osmdata.hpp
  osmtypes.hpp
  output-gazetteer.hpp
  output-multi.hpp
  output-null.hpp
  output-pgsql.hpp
  output.hpp
  parse-o5m.hpp
  parse-osmium.hpp
  parse.hpp
  pgsql.hpp
  processor-line.hpp
  processor-point.hpp
  processor-polygon.hpp
  reprojection.hpp
  sprompt.hpp
  table.hpp
  taginfo.hpp
  taginfo_impl.hpp
  tagtransform.hpp
  util.hpp
  wildcmp.hpp
)

add_library(osm2pgsql_lib STATIC ${osm2pgsql_lib_SOURCES})
set_target_properties(osm2pgsql_lib PROPERTIES OUTPUT_NAME osm2pgsql)

add_executable(osm2pgsql osm2pgsql.cpp)
target_link_libraries(osm2pgsql_lib ${LIBS})
target_link_libraries(osm2pgsql osm2pgsql_lib ${LIBS})

if (HAVE_SYS_TIME_H) # until node-persistent-cache-reader is translated to std::chrono
  add_executable(node-persistent-cache-reader node-persistent-cache-reader.cpp)
  target_link_libraries(node-persistent-cache-reader osm2pgsql_lib ${LIBS})
endif()

set(EXE_LOCATION $<TARGET_FILE:osm2pgsql>)

# Copy executable to source folder for Python testing
add_custom_command(TARGET osm2pgsql POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${EXE_LOCATION} ${CMAKE_CURRENT_SOURCE_DIR})

#############################################################
# Build tests
#############################################################

if (BUILD_TESTS)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()

#############################################################
# Install
#############################################################

install(TARGETS osm2pgsql DESTINATION bin)
install(FILES docs/osm2pgsql.1 DESTINATION share/man/man1)
install(FILES default.style empty.style 900913.sql DESTINATION share/osm2pgsql)
