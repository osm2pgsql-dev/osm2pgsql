ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = osm2pgsql nodecachefilereader
noinst_LTLIBRARIES = libosm2pgsql.la

osm2pgsql_SOURCES = osm2pgsql.cpp \
	binarysearcharray.hpp \
	geometry-builder.hpp \
	expire-tiles.hpp \
	input.hpp \
	keyvals.hpp \
	middle-pgsql.hpp \
	middle-ram.hpp \
	middle.hpp \
	node-persistent-cache.hpp \
	node-ram-cache.hpp \
	options.hpp \
	osmtypes.hpp \
	output-gazetteer.hpp \
	output-null.hpp \
	output-pgsql.hpp \
	output.hpp \
	parse.hpp \
	parse-o5m.hpp \
	parse-pbf.hpp \
	parse-primitive.hpp \
	parse-xml2.hpp \
	pgsql.hpp \
	rb.hpp \
	reprojection.hpp \
	sanitizer.hpp \
	sprompt.hpp \
	table.hpp \
	text-tree.hpp \
	util.hpp

osm2pgsql_LDADD = libosm2pgsql.la

libosm2pgsql_la_SOURCES = \
	UTF8sanitizer.cpp \
	binarysearcharray.cpp \
	buffer.cpp \
	expire-tiles.cpp \
	geometry-builder.cpp \
	geometry-processor.cpp \
	id-tracker.cpp \
	input.cpp \
	keyvals.cpp \
	middle.cpp \
	middle-pgsql.cpp \
	middle-ram.cpp \
	node-persistent-cache.cpp \
	node-ram-cache.cpp \
	options.cpp \
	osmtypes.cpp \
	output.cpp \
	output-gazetteer.cpp \
	output-multi.cpp \
	output-null.cpp \
	output-pgsql.cpp \
	parse.cpp \
	parse-o5m.cpp \
	parse-pbf.cpp \
	parse-primitive.cpp \
	parse-xml2.cpp \
	pgsql.cpp \
	pgsql-id-tracker.cpp \
	processor-line.cpp \
	processor-point.cpp \
	processor-polygon.cpp \
	rb.cpp \
	reprojection.cpp \
	sprompt.cpp \
	table.cpp \
	taginfo.cpp \
	tagtransform.cpp \
	text-tree.cpp \
	util.cpp \
	wildcmp.cpp

nodecachefilereader_SOURCES = node-persistent-cache-reader.cpp
nodecachefilereader_LDADD = libosm2pgsql.la

check_PROGRAMS = \
	tests/test-parse-xml2 \
	tests/test-middle-ram \
	tests/test-middle-pgsql \
	tests/test-output-multi-line \
	tests/test-output-multi-point \
	tests/test-output-multi-point-multi-table \
	tests/test-output-multi-polygon \
	tests/test-pgsql-escape \
	tests/test-parse-options \
	tests/test-expire-tiles

tests_test_parse_xml2_SOURCES = tests/test-parse-xml2.cpp 
tests_test_parse_xml2_LDADD = libosm2pgsql.la
tests_test_middle_ram_SOURCES = tests/test-middle-ram.cpp tests/middle-tests.cpp
tests_test_middle_ram_LDADD = libosm2pgsql.la
tests_test_middle_pgsql_SOURCES = tests/test-middle-pgsql.cpp tests/middle-tests.cpp tests/common-pg.cpp
tests_test_middle_pgsql_LDADD = libosm2pgsql.la
tests_test_output_multi_line_SOURCES = tests/test-output-multi-line.cpp tests/common-pg.cpp
tests_test_output_multi_line_LDADD = libosm2pgsql.la
tests_test_output_multi_point_SOURCES = tests/test-output-multi-point.cpp tests/common-pg.cpp
tests_test_output_multi_point_LDADD = libosm2pgsql.la
tests_test_output_multi_point_multi_table_SOURCES = tests/test-output-multi-point-multi-table.cpp tests/common-pg.cpp
tests_test_output_multi_point_multi_table_LDADD = libosm2pgsql.la
tests_test_output_multi_polygon_SOURCES = tests/test-output-multi-polygon.cpp tests/common-pg.cpp
tests_test_output_multi_polygon_LDADD = libosm2pgsql.la
tests_test_pgsql_escape_SOURCES = tests/test-pgsql-escape.cpp
tests_test_pgsql_escape_LDADD = libosm2pgsql.la
tests_test_parse_options_SOURCES = tests/test-parse-options.cpp
tests_test_parse_options_LDADD = libosm2pgsql.la
tests_test_expire_tiles_SOURCES = tests/test-expire-tiles.cpp
tests_test_expire_tiles_LDADD = libosm2pgsql.la

TESTS = $(check_PROGRAMS) tests/regression-test.sh
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = sh

if READER_PBF
osm2pgsql_SOURCES += parse-pbf.hpp fileformat.pb-c.h osmformat.pb-c.h
libosm2pgsql_la_SOURCES += parse-pbf.cpp fileformat.pb-c.c osmformat.pb-c.c

fileformat.pb-c.c: protobuf/fileformat.proto
	 $(AM_V_GEN) $(PROTOC_C) --proto_path=protobuf --c_out=. $<

fileformat.pb-c.h: fileformat.pb-c.c
	@if test ! -f $@; then \
	  rm -f $<; \
	  $(MAKE) $(AM_MAKEFLAGS) $<; \
	else :; fi

osmformat.pb-c.c: protobuf/osmformat.proto
	 $(AM_V_GEN) $(PROTOC_C) --proto_path=protobuf --c_out=. $<

osmformat.pb-c.h: osmformat.pb-c.c
	@if test ! -f $@; then \
	  rm -f $<; \
	  $(MAKE) $(AM_MAKEFLAGS) $<; \
	else :; fi

BUILT_SOURCES = \
  fileformat.pb-c.c fileformat.pb-c.h \
  osmformat.pb-c.c osmformat.pb-c.h

endif

osm2pgsqldir = $(datadir)/osm2pgsql

AM_CFLAGS = @PTHREAD_CFLAGS@ @LFS_CFLAGS@ @POSTGRESQL_CFLAGS@ @XML2_CFLAGS@ @BZIP2_CFLAGS@ @GEOS_CFLAGS@ @PROJ_CFLAGS@ @PROTOBUF_C_CFLAGS@ @ZLIB_CFLAGS@ -DOSM2PGSQL_DATADIR='"$(osm2pgsqldir)"' -DVERSION='"@PACKAGE_VERSION@"' @LUA_INCLUDE@
AM_CPPFLAGS = @PTHREAD_CFLAGS@ @POSTGRESQL_CFLAGS@ @XML2_CFLAGS@ @BZIP2_CFLAGS@ @GEOS_CFLAGS@ @PROJ_CFLAGS@ -DOSM2PGSQL_DATADIR='"$(osm2pgsqldir)"' -Igeos-fallback @LUA_INCLUDE@ @BOOST_CPPFLAGS@

GLOBAL_LDFLAGS = @PTHREAD_CFLAGS@ @ZLIB_LDFLAGS@ @ZLIB_LIBS@ @POSTGRESQL_LDFLAGS@ @POSTGRESQL_LIBS@ @XML2_LDFLAGS@ @BZIP2_LDFLAGS@ @BZIP2_LIBS@ @GEOS_LDFLAGS@ @GEOS_LIBS@ @PROJ_LDFLAGS@ @PROJ_LIBS@ @PROTOBUF_C_LDFLAGS@ @PROTOBUF_C_LIBS@ -L/usr/lib/x86_64-linux-gnu @LUA_LIB@ @BOOST_LDFLAGS@ @BOOST_FILESYSTEM_LIB@ @BOOST_SYSTEM_LIB@ @BOOST_THREAD_LIB@ @BOOST_ASIO_LIB@ -lstdc++
osm2pgsql_LDADD += $(GLOBAL_LDFLAGS)
tests_test_parse_xml2_LDADD += $(GLOBAL_LDFLAGS)
tests_test_middle_ram_LDADD += $(GLOBAL_LDFLAGS)
tests_test_middle_pgsql_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_line_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_point_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_point_multi_table_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_polygon_LDADD += $(GLOBAL_LDFLAGS)
tests_test_pgsql_escape_LDADD += $(GLOBAL_LDFLAGS)
tests_test_parse_options_LDADD += $(GLOBAL_LDFLAGS)
tests_test_expire_tiles_LDADD += $(GLOBAL_LDFLAGS)
nodecachefilereader_LDADD += $(GLOBAL_LDFLAGS)

osm2pgsql_DATA = default.style 900913.sql

man1_MANS = docs/osm2pgsql.1 docs/nodecachefilereader.1

EXTRA_DIST = osm2pgsql.spec.in \
             osm2pgsql.spec \
             protobuf/fileformat.proto \
             protobuf/osmformat.proto \
             debian \
             $(osm2pgsql_DATA)

$(PACKAGE).spec: $(PACKAGE).spec.in
	sed -e "s/@""PACKAGE""@/$(PACKAGE)/g; s/@""VERSION""@/$(VERSION)/g; s/@""SVN""@/`svnversion`/g;" $^ > $@

rpm: dist-gzip
	rpmbuild -ta $(distdir).tar.gz

distclean-local:
	@rm -f $(PACKAGE).spec
	@rm -f config.nice

test: check
