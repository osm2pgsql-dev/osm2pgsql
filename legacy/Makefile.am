bin_PROGRAMS = osm2pgsql-legacy

osm2pgsql_legacy_SOURCES = build_geometry.cpp osm2pgsql.c

AM_CFLAGS=@XML2_CFLAGS@ @GEOS_CFLAGS@ 
AM_CPPFLAGS=@XML2_CFLAGS@ @GEOS_CFLAGS@ 

AM_LDFLAGS=@XML2_LDFLAGS@ @GEOS_LDFLAGS@ @GEOS_LIBS@ 

noinst_HEADERS = build_geometry.h

