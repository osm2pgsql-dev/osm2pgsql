set(PACKAGE osm2pgsql)
set(PACKAGE_NAME osm2pgsql)
set(PACKAGE_VERSION 0.96.0)

cmake_minimum_required(VERSION 2.8.7)

project(osm2pgsql)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (WIN32)
    set(DEFAULT_STYLE "default.style" CACHE STRING "Default style used unless one is given on the command line")
else()
    set(DEFAULT_STYLE "${CMAKE_INSTALL_PREFIX}/share/osm2pgsql/default.style" CACHE STRING "Default style used unless one is given on the command line")
endif()

option(BUILD_TESTS "Build test suite" OFF)
option(WITH_LUA    "Build with Lua support" ON)
option(WITH_LUAJIT "Build with LuaJIT support" OFF)

if (NOT TESTING_TIMEOUT)
  set(TESTING_TIMEOUT 1200)
endif()

if (NOT WIN32 AND NOT APPLE)
  # No need for this path, just a workaround to make cmake check pass on all systems
  set(PostgreSQL_TYPE_INCLUDE_DIR /usr/include)
endif()

# Just in case user installed RPMs from http://yum.postgresql.org/
list(APPEND CMAKE_PREFIX_PATH /usr/pgsql-9.3 /usr/pgsql-9.4 /usr/pgsql-9.5 /usr/pgsql-9.6)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed, please use a separate build directory like `mkdir build && cd build && cmake ..`")
endif()

message(STATUS "Building osm2pgsql ${PACKAGE_VERSION}")

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set (CMAKE_CXX_STANDARD 11)
set (CMAKE_CXX_EXTENSIONS Off)
set (CMAKE_CXX_STANDARD_REQUIRED TRUE)

if ( MSVC )
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -wd4996)
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /ignore:4099 /STACK:30000000")
else()
  add_definitions(-Wall)
  add_definitions(-DBOOST_TEST_DYN_LINK)
  if (CMAKE_VERSION VERSION_LESS 3.1)
    add_definitions(-std=c++11)
  endif ()
endif()

option(EXTERNAL_LIBOSMIUM "Do not use the bundled libosmium" OFF)
option(EXTERNAL_PROTOZERO "Do not use the bundled protozero" OFF)

#############################################################
# Detect available headers and set global compiler options
#############################################################

include (CheckIncludeFiles)
include (CheckFunctionExists)
include (CheckTypeSize)

add_definitions( -DDEFAULT_STYLE=\"${DEFAULT_STYLE}\" )
add_definitions( -DFIXED_POINT )

CHECK_INCLUDE_FILES (termios.h HAVE_TERMIOS_H)
CHECK_INCLUDE_FILES (libgen.h HAVE_LIBGEN_H)
CHECK_INCLUDE_FILES (unistd.h HAVE_UNISTD_H)

if (WIN32)
  set(HAVE_LIBGEN_H FALSE)
endif()

#############################################################
# Find necessary libraries
#############################################################

if (NOT EXTERNAL_LIBOSMIUM)
  set(OSMIUM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/libosmium")
endif()

if (NOT EXTERNAL_PROTOZERO)
  set(PROTOZERO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/protozero/include")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(Osmium 2.14 REQUIRED COMPONENTS io proj)
include_directories(SYSTEM ${OSMIUM_INCLUDE_DIRS} ${PROTOZERO_INCLUDE_DIR})

if (WITH_LUA)
  if (WITH_LUAJIT)
    find_package(LuaJIT REQUIRED)
    include_directories(${LUAJIT_INCLUDE_DIR})
    set(HAVE_LUAJIT 1)
  else()
    find_package(Lua REQUIRED)
    include_directories(${LUA_INCLUDE_DIR})
  endif()
  set(HAVE_LUA 1)
endif()

if (NOT Boost_ADDITIONAL_VERSIONS)
  set(Boost_ADDITIONAL_VERSIONS "1.55;1.56;1.57;1.58;1.59;1.60;1.61")
endif()

# first try to find the version
find_package(Boost 1.50 REQUIRED COMPONENTS system filesystem ${BOOST_EXTRA})
include_directories(${Boost_INCLUDE_DIR})

find_package(PostgreSQL REQUIRED)
include_directories(${PostgreSQL_INCLUDE_DIRS})

find_package(Threads)

############### Libraries are found now ########################


set (LIBS ${Boost_LIBRARIES} ${PostgreSQL_LIBRARY} ${OSMIUM_LIBRARIES})

if (LUAJIT_FOUND)
  list(APPEND LIBS ${LUAJIT_LIBRARIES})
elseif(LUA_FOUND)
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

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

if (NOT HAVE_UNISTD_H AND NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/unistd.h)
   file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/unistd.h "// empty header\n")
endif()

set(osm2pgsql_lib_SOURCES
  db-copy.cpp
  expire-tiles.cpp
  geometry-processor.cpp
  id-tracker.cpp
  middle-pgsql.cpp
  middle-ram.cpp
  node-persistent-cache.cpp
  node-ram-cache.cpp
  options.cpp
  osmdata.cpp
  osmium-builder.cpp
  gazetteer-style.cpp
  output-gazetteer.cpp
  output-multi.cpp
  output-null.cpp
  output-pgsql.cpp
  output.cpp
  parse-osmium.cpp
  pgsql.cpp
  processor-line.cpp
  processor-point.cpp
  processor-polygon.cpp
  reprojection.cpp
  sprompt.cpp
  table.cpp
  taginfo.cpp
  tagtransform.cpp
  tagtransform-c.cpp
  util.cpp
  wildcmp.cpp
  db-copy.hpp
  expire-tiles.hpp
  geometry-processor.hpp
  id-tracker.hpp
  middle-pgsql.hpp
  middle-ram.hpp
  middle.hpp
  node-persistent-cache.hpp
  node-ram-cache.hpp
  options.hpp
  osmdata.hpp
  osmium-builder.hpp
  osmtypes.hpp
  gazetteer-style.hpp
  output-gazetteer.hpp
  output-multi.hpp
  output-null.hpp
  output-pgsql.hpp
  output.hpp
  parse-osmium.hpp
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
  wkb.hpp
)

if (LUA_FOUND OR LUAJIT_FOUND)
  list(APPEND osm2pgsql_lib_SOURCES tagtransform-lua.cpp)
endif()

add_library(osm2pgsql_lib STATIC ${osm2pgsql_lib_SOURCES})
set_target_properties(osm2pgsql_lib PROPERTIES OUTPUT_NAME osm2pgsql)

add_executable(osm2pgsql osm2pgsql.cpp)
target_link_libraries(osm2pgsql_lib ${LIBS})
target_link_libraries(osm2pgsql osm2pgsql_lib ${LIBS})

#############################################################
# Build tests
#############################################################

enable_testing()
include(CTest)

if (BUILD_TESTS)
  add_subdirectory(tests)
else()
  add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()


#############################################################
# Install
#############################################################

install(TARGETS osm2pgsql DESTINATION bin)
install(FILES docs/osm2pgsql.1 DESTINATION share/man/man1)
install(FILES default.style empty.style DESTINATION share/osm2pgsql)
