
message(STATUS "Looking for pandoc")
find_program(PANDOC pandoc)

if(PANDOC)
    message(STATUS "Looking for pandoc - found")
    message(STATUS "  Manual page can be built using 'man' target")
    set(PANDOC_MAN_OPTIONS
        -s
        -t man
        --template ${CMAKE_CURRENT_SOURCE_DIR}/manpage.template
        --variable "version=${PACKAGE_VERSION}"
        --variable "title=OSM2PGSQL"
        --variable "section=1"
    )

    add_custom_command(OUTPUT osm2pgsql.1
        COMMAND ${PANDOC} ${PANDOC_MAN_OPTIONS} -o osm2pgsql.1
            ${CMAKE_CURRENT_SOURCE_DIR}/osm2pgsql.md
        DEPENDS osm2pgsql.md manpage.template
        COMMENT "Building manpage osm2pgsql.1"
        VERBATIM)

    add_custom_target(man DEPENDS osm2pgsql.1 VERBATIM)
else()
    message(STATUS "Looking for pandoc - not found")
    message(STATUS "  Manual page can not be built")
endif()

install(FILES osm2pgsql.1 DESTINATION share/man/man1)

