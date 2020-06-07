
set(osm2pgsql_lib_SOURCES
  dependency-manager.cpp
  db-copy.cpp
  expire-tiles.cpp
  gazetteer-style.cpp
  geometry-processor.cpp
  geom-transform.cpp
  id-tracker.cpp
  input-handler.cpp
  middle-pgsql.cpp
  middle-ram.cpp
  node-persistent-cache.cpp
  node-ram-cache.cpp
  options.cpp
  osmdata.cpp
  osmium-builder.cpp
  output-gazetteer.cpp
  output-multi.cpp
  output-null.cpp
  output-pgsql.cpp
  output.cpp
  pgsql.cpp
  processor-line.cpp
  processor-point.cpp
  processor-polygon.cpp
  progress-display.cpp
  reprojection.cpp
  sprompt.cpp
  table.cpp
  taginfo.cpp
  tagtransform-c.cpp
  tagtransform.cpp
  util.cpp
  wildcmp.cpp
  dependency-manager.hpp
  db-copy.hpp
  expire-tiles.hpp
  gazetteer-style.hpp
  geometry-processor.hpp
  geom-transform.hpp
  id-tracker.hpp
  input-handler.hpp
  middle-pgsql.hpp
  middle-ram.hpp
  middle.hpp
  node-persistent-cache.hpp
  node-ram-cache.hpp
  options.hpp
  osmdata.hpp
  osmium-builder.hpp
  osmtypes.hpp
  output-gazetteer.hpp
  output-multi.hpp
  output-null.hpp
  output-pgsql.hpp
  output.hpp
  pgsql.hpp
  processor-line.hpp
  processor-point.hpp
  processor-polygon.hpp
  progress-display.hpp
  reprojection.hpp
  sprompt.hpp
  table.hpp
  taginfo-impl.hpp
  taginfo.hpp
  tagtransform.hpp
  util.hpp
  wildcmp.hpp
  wkb.hpp
)

if (LUA_FOUND OR LUAJIT_FOUND)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/init.lua")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/init.lua" LUA_INIT_CODE)
    configure_file(lua-init.cpp.in lua-init.cpp @ONLY)
    list(APPEND osm2pgsql_lib_SOURCES
        flex-table.cpp
        flex-table-column.cpp
        lua-utils.cpp
        output-flex.cpp
        tagtransform-lua.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/lua-init.cpp)
endif()

list(APPEND osm2pgsql_lib_SOURCES ${PROJECT_BINARY_DIR}/src/version.cpp)

if (HAVE_PROJ4)
    list(APPEND osm2pgsql_lib_SOURCES reprojection-generic-proj4.cpp)
    if (NOT MSVC)
        set_source_files_properties(reprojection-generic-proj4.cpp
            PROPERTIES COMPILE_FLAGS -Wno-deprecated-declarations)
    endif()
else()
    list(APPEND osm2pgsql_lib_SOURCES reprojection-generic-none.cpp)
endif()

add_library(osm2pgsql_lib STATIC ${osm2pgsql_lib_SOURCES})
set_target_properties(osm2pgsql_lib PROPERTIES OUTPUT_NAME osm2pgsql)
target_link_libraries(osm2pgsql_lib ${LIBS})

