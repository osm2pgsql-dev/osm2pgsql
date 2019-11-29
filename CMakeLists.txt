set(PACKAGE osm2pgsql)
set(PACKAGE_NAME osm2pgsql)
set(PACKAGE_VERSION 1.2.0)

cmake_minimum_required(VERSION 2.8.7)

project(osm2pgsql)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (WIN32)
    set(DEFAULT_STYLE "default.style" CACHE STRING "Default style used unless one is given on the command line")
else()
    set(DEFAULT_STYLE "${CMAKE_INSTALL_PREFIX}/share/osm2pgsql/default.style" CACHE STRING "Default style used unless one is given on the command line")
endif()

option(BUILD_TESTS "Build test suite" OFF)
option(WITH_LUA    "Build with Lua support" ON)
option(WITH_LUAJIT "Build with LuaJIT support" OFF)

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

if (NOT "${CMAKE_CXX_STANDARD}")
    set(CMAKE_CXX_STANDARD 11)
endif()
message(STATUS "Building in C++${CMAKE_CXX_STANDARD} mode")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -wd4996)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /ignore:4099 /STACK:30000000")
else()
    add_definitions(-Wall)
    if (CMAKE_VERSION VERSION_LESS 3.1)
        add_definitions(-std=c++11)
    endif()
endif()

option(EXTERNAL_LIBOSMIUM "Do not use the bundled libosmium" OFF)
option(EXTERNAL_PROTOZERO "Do not use the bundled protozero" OFF)
option(EXTERNAL_FMT       "Do not use the bundled fmt"       OFF)

#############################################################
# Detect available headers and set global compiler options
#############################################################

include(CheckIncludeFiles)

add_definitions(-DDEFAULT_STYLE=\"${DEFAULT_STYLE}\")

CHECK_INCLUDE_FILES(termios.h HAVE_TERMIOS_H)
CHECK_INCLUDE_FILES(unistd.h HAVE_UNISTD_H)

#############################################################
# Find necessary libraries
#############################################################

if (NOT EXTERNAL_LIBOSMIUM)
    set(OSMIUM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/libosmium")
endif()

if (NOT EXTERNAL_PROTOZERO)
    set(PROTOZERO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/protozero/include")
endif()

if (NOT EXTERNAL_FMT)
    set(FMT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/fmt/include")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})

find_package(Osmium 2.14 REQUIRED COMPONENTS io proj)
include_directories(SYSTEM ${OSMIUM_INCLUDE_DIRS} ${PROTOZERO_INCLUDE_DIR} ${FMT_INCLUDE_DIR})

if (WITH_LUA)
    if (WITH_LUAJIT)
        find_package(LuaJIT REQUIRED)
        include_directories(SYSTEM ${LUAJIT_INCLUDE_DIR})
        set(HAVE_LUAJIT 1)
    else()
        find_package(Lua REQUIRED)
        include_directories(SYSTEM ${LUA_INCLUDE_DIR})
    endif()
    set(HAVE_LUA 1)
endif()

find_package(Boost 1.50 REQUIRED COMPONENTS system filesystem)
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

find_package(PostgreSQL REQUIRED)
include_directories(SYSTEM ${PostgreSQL_INCLUDE_DIRS})

find_package(Threads)

############### Libraries are found now ########################

set(LIBS ${Boost_LIBRARIES} ${PostgreSQL_LIBRARY} ${OSMIUM_LIBRARIES})

if (LUAJIT_FOUND)
    list(APPEND LIBS ${LUAJIT_LIBRARIES})
elseif(LUA_FOUND)
    list(APPEND LIBS ${LUA_LIBRARIES})
endif()

if (WIN32)
    list(APPEND LIBS ws2_32)
    if (MSVC)
        find_path(GETOPT_INCLUDE_DIR getopt.h)
        find_library(GETOPT_LIBRARY NAMES wingetopt getopt)
        if (GETOPT_INCLUDE_DIR AND GETOPT_LIBRARY)
            include_directories(SYSTEM ${GETOPT_INCLUDE_DIR})
            list(APPEND LIBS ${GETOPT_LIBRARY})
        else()
            message(ERROR "Can not find getopt library for Windows. Please get it from https://github.com/alex85k/wingetopt or alternative source.")
        endif()
    endif()
endif()

message(STATUS "Libraries used to build: ${LIBS}")

#############################################################
# Build the library and executable file
#############################################################

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

if (NOT HAVE_UNISTD_H AND NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/unistd.h)
   file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/unistd.h "// empty header\n")
endif()

add_subdirectory(src)

add_executable(osm2pgsql src/osm2pgsql.cpp)
target_link_libraries(osm2pgsql osm2pgsql_lib ${LIBS})

#############################################################
# Optional "clang-tidy" target
#############################################################

message(STATUS "Looking for clang-tidy")
find_program(CLANG_TIDY
             NAMES clang-tidy clang-tidy-10 clang-tidy-9 clang-tidy-8 clang-tidy-7)

if(CLANG_TIDY)
    message(STATUS "Looking for clang-tidy - found ${CLANG_TIDY}")

    file(GLOB CT_CHECK_FILES src/*.cpp tests/*cpp)

    add_custom_target(clang-tidy
        ${CLANG_TIDY}
        -p ${CMAKE_BINARY_DIR}
        ${CT_CHECK_FILES}
    )
else()
    message(STATUS "Looking for clang-tidy - not found")
    message(STATUS "  Build target 'clang-tidy' will not be available.")
endif()

#############################################################
# Build tests
#############################################################

if (BUILD_TESTS)
    enable_testing()
    if (NOT TESTING_TIMEOUT)
        set(TESTING_TIMEOUT 1200)
    endif()
    add_subdirectory(tests)
else()
    message(STATUS "Tests disabled. Set BUILD_TESTS=ON to enable tests.")
endif()

#############################################################
# Install
#############################################################

install(TARGETS osm2pgsql DESTINATION bin)
install(FILES docs/osm2pgsql.1 DESTINATION share/man/man1)
install(FILES default.style empty.style DESTINATION share/osm2pgsql)
