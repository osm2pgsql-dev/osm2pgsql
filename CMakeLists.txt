
cmake_minimum_required(VERSION 3.5.0)

project(osm2pgsql VERSION 1.6.0 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Do not create install targets when run as a subproject.
# Currently used by Nominatim which cannot yet rely on installed versions
# of osm2pgsql.
if (${PROJECT_NAME} STREQUAL ${CMAKE_PROJECT_NAME})
    set(ENABLE_INSTALL ON)
else()
    set(ENABLE_INSTALL OFF)
endif()

if (WIN32)
    set(DEFAULT_STYLE "default.style" CACHE STRING "Default style used unless one is given on the command line")
else()
    set(DEFAULT_STYLE "${CMAKE_INSTALL_PREFIX}/share/osm2pgsql/default.style" CACHE STRING "Default style used unless one is given on the command line")
endif()

option(BUILD_TESTS    "Build test suite" OFF)
option(BUILD_COVERAGE "Build with coverage" OFF)
option(WITH_LUA       "Build with Lua support" ON)
option(WITH_LUAJIT    "Build with LuaJIT support" OFF)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed, please use a separate build directory like `mkdir build && cd build && cmake ..`")
endif()

message(STATUS "Building osm2pgsql ${PROJECT_VERSION}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if (NOT "${CMAKE_CXX_STANDARD}")
    set(CMAKE_CXX_STANDARD 17)
endif()
message(STATUS "Building in C++${CMAKE_CXX_STANDARD} mode")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)
    add_compile_options(-wd4996)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /ignore:4099 /STACK:30000000")
else()
    add_compile_options(-Wall)
endif()

option(EXTERNAL_LIBOSMIUM "Do not use the bundled libosmium" OFF)
option(EXTERNAL_PROTOZERO "Do not use the bundled protozero" OFF)
option(EXTERNAL_FMT       "Do not use the bundled fmt"       OFF)
option(EXTERNAL_RAPIDJSON "Do not use the bundled rapidjson" OFF)

set(USE_PROJ_LIB "auto" CACHE STRING "Which version of PROJ API to use: ('4', '6', 'off', or 'auto')")

if (NOT WIN32 AND NOT APPLE)
    # No need for this path, just a workaround to make cmake work on all systems.
    # Without this we need the PostgreSQL server libraries installed.
    # https://stackoverflow.com/questions/13920383/findpostgresql-cmake-wont-work-on-ubuntu
    set(PostgreSQL_TYPE_INCLUDE_DIR /usr/include)
endif()

set(MINIMUM_POSTGRESQL_SERVER_VERSION "9.5")
set(MINIMUM_POSTGRESQL_SERVER_VERSION_NUM "90500")

set(PostgreSQL_ADDITIONAL_VERSIONS "14" "13" "12" "11" "10" "9.6" "9.5")

#############################################################
# Version
#############################################################

find_package(Git)

if (GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty=-changed
                    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                    OUTPUT_VARIABLE VERSION_FROM_GIT
                    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (VERSION_FROM_GIT)
        set(VERSION_FROM_GIT " (${VERSION_FROM_GIT})")
    endif()
endif()

configure_file(
    ${PROJECT_SOURCE_DIR}/src/version.cpp.in
    ${PROJECT_BINARY_DIR}/src/version.cpp
)

#############################################################
# Coverage
#############################################################

if (BUILD_COVERAGE)
    if (NOT BUILD_TESTS)
        message(WARNING "Coverage build enabled, but tests not enabled!")
    endif()

    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        string(REGEX REPLACE "^([0-9]+).*$" "llvm-cov-\\1"
               gcov_ ${CMAKE_CXX_COMPILER_VERSION})
        set(gcov_param_ " gcov")
    else()
        set(gcov_ "gcov")
        set(gcov_param_ "")
    endif()

    message(STATUS "Looking for coverage tool: ${gcov_}")

    find_program(GCOV ${gcov_} DOC "Coverage tool")

    if (GCOV STREQUAL "GCOV-NOTFOUND")
        message(STATUS "  coverage tool not found - coverage disabled")
    else()
        message(STATUS "  found")

        message(STATUS "Looking von gcovr")
        find_program(GCOVR "gcovr" DOC "Coverage report tool")
        if (GCOVR STREQUAL "GCOVR-NOTFOUND")
            message(WARNING "  gcovr not found - coverage will not work")
        else()
            message(STATUS "  found")
        endif()

        add_compile_options(-g -O0 -fno-inline-functions -fno-inline --coverage)

        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage"
            CACHE
            STRING "Flags used by the linker during all build types."
            FORCE)

        set(coverage_report_dir "${CMAKE_BINARY_DIR}/coverage")
        file(MAKE_DIRECTORY ${coverage_report_dir})
        add_custom_target(coverage
            ${GCOVR}
            ${CMAKE_BINARY_DIR}
            --root=${CMAKE_SOURCE_DIR}
            --html-details
            --html-title "osm2pgsql coverage report"
            #--verbose
            #--keep
            --delete
            '--exclude=.*contrib.*'
            --sort-percentage
            --gcov-executable=${GCOV}${gcov_param_}
            --output=${coverage_report_dir}/index.html)
    endif()
endif()

#############################################################
# Detect available headers and set global compiler options
#############################################################

include(CheckIncludeFiles)

add_definitions(-DDEFAULT_STYLE=\"${DEFAULT_STYLE}\")

#############################################################
# Find necessary libraries
#############################################################

if (NOT EXTERNAL_LIBOSMIUM)
    set(OSMIUM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/libosmium/include")
endif()

if (NOT EXTERNAL_PROTOZERO)
    set(PROTOZERO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/protozero/include")
endif()

if (NOT EXTERNAL_FMT)
    set(FMT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/fmt/include")
endif()

if (NOT EXTERNAL_RAPIDJSON)
    set(RAPIDJSON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/rapidjson/include")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})

find_package(Osmium 2.17.3 REQUIRED COMPONENTS io)
include_directories(SYSTEM ${OSMIUM_INCLUDE_DIRS} ${PROTOZERO_INCLUDE_DIR} ${FMT_INCLUDE_DIR} ${RAPIDJSON_INCLUDE_DIR})

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
    find_program(LUA_EXE lua)
endif()

find_package(Boost 1.50 REQUIRED COMPONENTS system filesystem)
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

find_package(PostgreSQL REQUIRED)
include_directories(SYSTEM ${PostgreSQL_INCLUDE_DIRS})

find_package(Threads)

############### Libraries are found now ########################

set(LIBS ${Boost_LIBRARIES} ${PostgreSQL_LIBRARY} ${OSMIUM_LIBRARIES})

if (USE_PROJ_LIB STREQUAL "off")
    message(STATUS "Proj library disabled (because USE_PROJ_LIB is set to 'off').")
else()
    find_path(PROJ4_INCLUDE_DIR proj_api.h)
    if (PROJ4_INCLUDE_DIR AND NOT USE_PROJ_LIB STREQUAL "6")
        message(STATUS "Found proj_api.h")
        find_library(PROJ_LIBRARY NAMES proj)
        message(STATUS "Found Proj [API 4] ${PROJ_LIBRARY}")
        add_definitions(-DHAVE_GENERIC_PROJ=4)
        set(HAVE_PROJ4 1)
        list(APPEND LIBS ${PROJ_LIBRARY})
        include_directories(SYSTEM ${PROJ4_INCLUDE_DIR})
    elseif (NOT USE_PROJ_LIB STREQUAL "4")
        find_path(PROJ6_INCLUDE_DIR proj.h)
        find_library(PROJ_LIBRARY NAMES proj)
        if (PROJ_LIBRARY)
            message(STATUS "Found Proj [API 6] ${PROJ_LIBRARY}")
            add_definitions(-DHAVE_GENERIC_PROJ=6)
            set(HAVE_PROJ6 1)
            list(APPEND LIBS ${PROJ_LIBRARY})
            include_directories(SYSTEM ${PROJ6_INCLUDE_DIR})
        else()
            message(STATUS "Proj library not found.")
            message(STATUS "    Only Mercartor and WGS84 projections will be available.")
        endif()
    endif()
endif()

if (USE_PROJ_LIB STREQUAL "4" AND NOT HAVE_PROJ4)
    message(FATAL_ERROR "USE_PROJ_LIB was set to '4', but PROJ version 4 API not found")
endif()

if (USE_PROJ_LIB STREQUAL "6" AND NOT HAVE_PROJ6)
    message(FATAL_ERROR "USE_PROJ_LIB was set to '6', but PROJ version 6 API not found")
endif()

if (LUAJIT_FOUND)
    list(APPEND LIBS ${LUAJIT_LIBRARIES})
elseif (LUA_FOUND)
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
            message(FATAL_ERROR "Can not find getopt library for Windows. Please get it from https://github.com/alex85k/wingetopt or alternative source.")
        endif()
    endif()
endif()

message(STATUS "Libraries used to build: ${LIBS}")

#############################################################
# Build the library and executable file
#############################################################

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_subdirectory(src)

add_executable(osm2pgsql src/osm2pgsql.cpp)
target_link_libraries(osm2pgsql osm2pgsql_lib ${LIBS})

#############################################################
# Optional "clang-tidy" target
#############################################################

message(STATUS "Looking for clang-tidy")
find_program(CLANG_TIDY
             NAMES clang-tidy clang-tidy-14 clang-tidy-13 clang-tidy-12 clang-tidy-11)

if (CLANG_TIDY)
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
# Man page
#############################################################

add_subdirectory(docs)

#############################################################
# Install
#############################################################

if (ENABLE_INSTALL)
    install(TARGETS osm2pgsql DESTINATION bin)
    install(PROGRAMS scripts/osm2pgsql-replication DESTINATION bin)
    install(FILES default.style empty.style DESTINATION share/osm2pgsql)
endif()
