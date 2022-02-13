message(STATUS "Looking for pandoc")
find_program(PANDOC pandoc)
message(STATUS "Looking for argparse-manpage")
find_program(ARGPARSE_MANPAGE argparse-manpage)

set(MANPAGE_TARGETS "")

if(PANDOC)
    message(STATUS "Looking for pandoc - found")
    message(STATUS "  osm2pgsql manual page can be built using 'man' target")
    set(PANDOC_MAN_OPTIONS
        -s
        -t man
        --template ${CMAKE_CURRENT_SOURCE_DIR}/manpage.template
        --variable "version=${PROJECT_VERSION}"
        --variable "title=OSM2PGSQL"
        --variable "section=1"
    )

    add_custom_command(OUTPUT osm2pgsql.1
        COMMAND ${PANDOC} ${PANDOC_MAN_OPTIONS} -o osm2pgsql.1
            ${CMAKE_CURRENT_SOURCE_DIR}/osm2pgsql.md
        DEPENDS osm2pgsql.md manpage.template
        COMMENT "Building manpage osm2pgsql.1"
        VERBATIM)

    list(APPEND MANPAGE_TARGETS osm2pgsql.1)
else()
    message(STATUS "Looking for pandoc - not found")
    message(STATUS "  osm2pgsql manual page can not be built")
endif()

if(ARGPARSE_MANPAGE)
    message(STATUS "Looking for argparse-manpage - found")
    message(STATUS "  osm2pgsql-replication manual page can be built using 'man' target")

    add_custom_command(OUTPUT osm2pgsql-replication.1
        COMMAND ${ARGPARSE_MANPAGE} --pyfile ${CMAKE_SOURCE_DIR}/scripts/osm2pgsql-replication --function get_parser --project-name osm2pgsql-replication --url osm2pgsql.org > ${CMAKE_CURRENT_BINARY_DIR}/osm2pgsql-replication.1
        DEPENDS ${CMAKE_SOURCE_DIR}/scripts/osm2pgsql-replication
        COMMENT "Building manpage osm2pgsql-replication.1"
        VERBATIM)

    list(APPEND MANPAGE_TARGETS osm2pgsql-replication.1)
else()
    message(STATUS "Looking for argparse-manpage - not found")
    message(STATUS "  osm2pgsql-replication page can not be built")
endif()

if(MANPAGE_TARGETS)
    add_custom_target(man DEPENDS ${MANPAGE_TARGETS} VERBATIM)
endif()

if(ENABLE_INSTALL)
    install(FILES osm2pgsql.1 DESTINATION share/man/man1)
    install(FILES osm2pgsql-replication.1 DESTINATION share/man/man1)
endif()
