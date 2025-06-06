
cmake_minimum_required(VERSION 3.10.0)

project(osm2pgsql VERSION 2.1.1 LANGUAGES CXX C)

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

if (NOT ${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    message(FATAL_ERROR "osm2pgsql needs a 64 bit architecture")
endif()

if (WIN32)
    set(DEFAULT_STYLE "default.style" CACHE STRING "Default style used unless one is given on the command line")
else()
    set(DEFAULT_STYLE "${CMAKE_INSTALL_PREFIX}/share/osm2pgsql/default.style" CACHE STRING "Default style used unless one is given on the command line")
endif()

option(BUILD_TESTS    "Build test suite" OFF)
option(BUILD_COVERAGE "Build with coverage" OFF)
option(WITH_LUAJIT    "Build with LuaJIT support" OFF)
option(WITH_PROJ      "Build with Projection support" ON)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed, please use a separate build directory like `mkdir build && cd build && cmake ..`")
endif()

message(STATUS "Building osm2pgsql ${PROJECT_VERSION}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# We don't want to use special compiler extensions because we want portability
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)
    add_compile_options(-wd4996 -utf-8)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /ignore:4099")
else()
    add_compile_options(-Wall)
endif()

option(EXTERNAL_LIBOSMIUM "Do not use the bundled libosmium" OFF)
option(EXTERNAL_PROTOZERO "Do not use the bundled protozero" OFF)
option(EXTERNAL_FMT       "Do not use the bundled fmt"       OFF)
option(EXTERNAL_CLI11     "Do not use the bundled CLI11"     OFF)

if (NOT WIN32 AND NOT APPLE)
    # No need for this path, just a workaround to make cmake work on all systems.
    # Without this we need the PostgreSQL server libraries installed.
    # https://stackoverflow.com/questions/13920383/findpostgresql-cmake-wont-work-on-ubuntu
    set(PostgreSQL_TYPE_INCLUDE_DIR /usr/include)
endif()

set(MINIMUM_POSTGRESQL_SERVER_VERSION "11")
set(MINIMUM_POSTGRESQL_SERVER_VERSION_NUM "110000")

set(PostgreSQL_ADDITIONAL_VERSIONS "17" "16" "15" "14" "13" "12" "11")

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

if (NOT EXTERNAL_CLI11)
    set(CLI11_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contrib/CLI11/include")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})

find_package(Osmium 2.17.3 REQUIRED COMPONENTS io)
include_directories(SYSTEM ${OSMIUM_INCLUDE_DIRS} ${PROTOZERO_INCLUDE_DIR} ${FMT_INCLUDE_DIR} ${CLI11_INCLUDE_DIR})

if (WITH_LUAJIT)
    message(STATUS "Building with LuaJIT support")
    find_package(LuaJIT REQUIRED)
    include_directories(SYSTEM ${LUAJIT_INCLUDE_DIR})
    add_definitions(-DHAVE_LUAJIT=1)
else()
    message(STATUS "Building with Lua (but not LuaJIT) support")
    find_package(Lua REQUIRED)
    include_directories(SYSTEM ${LUA_INCLUDE_DIR})
endif()

find_program(LUA_EXE NAMES lua lua5.4 lua5.3 lua5.2 lua5.1)

find_package(Boost 1.50 REQUIRED)
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

find_package(PostgreSQL REQUIRED)
include_directories(SYSTEM ${PostgreSQL_INCLUDE_DIRS})

find_package(Threads)

find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp)
include_directories(SYSTEM ${NLOHMANN_INCLUDE_DIR})

find_path(POTRACE_INCLUDE_DIR potracelib.h)
find_library(POTRACE_LIBRARY NAMES potrace)

find_package(OpenCV QUIET OPTIONAL_COMPONENTS core imgcodecs imgproc)

############### Libraries are found now ########################

set(LIBS ${Boost_LIBRARIES} ${PostgreSQL_LIBRARY} ${OSMIUM_LIBRARIES})

if (WITH_PROJ)
    find_path(PROJ6_INCLUDE_DIR proj.h)
    find_library(PROJ_LIBRARY NAMES proj)
    if (PROJ_LIBRARY)
        message(STATUS "Found Proj ${PROJ_LIBRARY}")
        add_definitions(-DHAVE_GENERIC_PROJ=6)
        set(HAVE_PROJ6 1)
        list(APPEND LIBS ${PROJ_LIBRARY})
        include_directories(SYSTEM ${PROJ6_INCLUDE_DIR})
    endif()
endif()

if (LUAJIT_FOUND)
    list(APPEND LIBS ${LUAJIT_LIBRARIES})
else()
    list(APPEND LIBS ${LUA_LIBRARIES})
endif()

if (WIN32)
    list(APPEND LIBS ws2_32)
endif()

message(STATUS "Libraries used to build: ${LIBS}")

#############################################################
# Build the library and executable file
#############################################################

add_subdirectory(src)

add_executable(osm2pgsql src/osm2pgsql.cpp)
target_link_libraries(osm2pgsql osm2pgsql_lib ${LIBS})

if (${POTRACE_LIBRARY} STREQUAL "POTRACE_LIBRARY-NOTFOUND" OR NOT OPENCV_CORE_FOUND)
    message(STATUS "Did not find opencv and/or potrace library. Not building osm2pgsql-gen.")
else()
    message(STATUS "Found opencv and potrace library. Building osm2pgsql-gen.")
    set(BUILD_GEN 1)
    include_directories(SYSTEM ${POTRACE_INCLUDE_DIR})
    add_executable(osm2pgsql-gen src/gen/osm2pgsql-gen.cpp
                src/gen/canvas.cpp
                src/gen/gen-base.cpp
                src/gen/gen-create.cpp
                src/gen/gen-discrete-isolation.cpp
                src/gen/gen-rivers.cpp
                src/gen/gen-tile-builtup.cpp
                src/gen/gen-tile-raster.cpp
                src/gen/gen-tile-sql.cpp
                src/gen/gen-tile-vector.cpp
                src/gen/gen-tile.cpp
                src/gen/raster.cpp
                src/gen/tracer.cpp)
    target_link_libraries(osm2pgsql-gen osm2pgsql_lib ${LIBS} ${POTRACE_LIBRARY} ${OpenCV_LIBS})
endif()

#############################################################
# Optional "clang-tidy" target
#############################################################

message(STATUS "Looking for clang-tidy")
find_program(CLANG_TIDY
             NAMES clang-tidy clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16 clang-tidy-15)

if (CLANG_TIDY)
    message(STATUS "Looking for clang-tidy - found ${CLANG_TIDY}")

    file(GLOB CT_CHECK_FILES src/*.cpp src/*/*.cpp tests/*cpp)

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
    configure_file(
        ${PROJECT_SOURCE_DIR}/tests/run-behave.in
        ${PROJECT_BINARY_DIR}/run-behave
    )
else()
    message(STATUS "Tests disabled. Set BUILD_TESTS=ON to enable tests.")
endif()

#############################################################
# Man page
#############################################################

add_subdirectory(man)

#############################################################
# Install
#############################################################

include(GNUInstallDirs)

if (ENABLE_INSTALL)
    install(TARGETS osm2pgsql DESTINATION bin)
    install(FILES default.style empty.style DESTINATION share/osm2pgsql)
    install(PROGRAMS scripts/osm2pgsql-replication DESTINATION bin)
    if (BUILD_GEN)
        install(TARGETS osm2pgsql-gen DESTINATION bin)
    endif()
endif()
