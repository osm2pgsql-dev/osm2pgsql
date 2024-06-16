
add_library(osm2pgsql_lib STATIC)

# Set the minimum required C++ version for the library and hence for all
# binaries that use it.
target_compile_features(osm2pgsql_lib PUBLIC cxx_std_17)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/init.lua")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/init.lua" LUA_INIT_CODE)
configure_file(lua-init.cpp.in lua-init.cpp @ONLY)

target_sources(osm2pgsql_lib PRIVATE
    command-line-app.cpp
    command-line-parser.cpp
    db-copy.cpp
    debug-output.cpp
    expire-output.cpp
    expire-tiles.cpp
    flex-index.cpp
    flex-lua-expire-output.cpp
    flex-lua-geom.cpp
    flex-lua-index.cpp
    flex-lua-table.cpp
    flex-table-column.cpp
    flex-table.cpp
    flex-write.cpp
    geom-box.cpp
    geom-from-osm.cpp
    geom-functions.cpp
    geom-pole-of-inaccessibility.cpp
    geom.cpp
    idlist.cpp
    input.cpp
    logging.cpp
    lua-setup.cpp
    lua-utils.cpp
    middle-pgsql.cpp
    middle-ram.cpp
    middle.cpp
    node-locations.cpp
    node-persistent-cache.cpp
    ordered-index.cpp
    osmdata.cpp
    output-flex.cpp
    output-null.cpp
    output-pgsql.cpp
    output.cpp
    pgsql-capabilities.cpp
    pgsql-helper.cpp
    pgsql.cpp
    progress-display.cpp
    properties.cpp
    reprojection.cpp
    table.cpp
    taginfo.cpp
    tagtransform-c.cpp
    tagtransform-lua.cpp
    tagtransform.cpp
    thread-pool.cpp
    tile.cpp
    util.cpp
    wildcmp.cpp
    wkb.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/lua-init.cpp
    ${PROJECT_BINARY_DIR}/src/version.cpp
)

if (HAVE_PROJ6)
    target_sources(osm2pgsql_lib PRIVATE reprojection-generic-proj6.cpp)
else()
    target_sources(osm2pgsql_lib PRIVATE reprojection-generic-none.cpp)
endif()

set_target_properties(osm2pgsql_lib PROPERTIES OUTPUT_NAME osm2pgsql)
target_link_libraries(osm2pgsql_lib ${LIBS})

