
set(osm2pgsql_lib_SOURCES
  db-copy.cpp
  expire-tiles.cpp
  gazetteer-style.cpp
  geometry-processor.cpp
  id-tracker.cpp
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
  parse-osmium.cpp
  pgsql.cpp
  processor-line.cpp
  processor-point.cpp
  processor-polygon.cpp
  reprojection.cpp
  sprompt.cpp
  table.cpp
  taginfo.cpp
  tagtransform-c.cpp
  tagtransform.cpp
  util.cpp
  wildcmp.cpp
  db-copy.hpp
  expire-tiles.hpp
  gazetteer-style.hpp
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
  taginfo-impl.hpp
  taginfo.hpp
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
target_link_libraries(osm2pgsql_lib ${LIBS})

