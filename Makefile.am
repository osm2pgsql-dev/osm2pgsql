ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = osm2pgsql nodecachefilereader
noinst_LTLIBRARIES = libosm2pgsql.la

osm2pgsql_SOURCES = osm2pgsql.cpp

osm2pgsql_LDADD = libosm2pgsql.la

libosm2pgsql_la_SOURCES = \
	expire-tiles.cpp \
	geometry-builder.cpp \
	geometry-processor.cpp \
	id-tracker.cpp \
	middle.cpp \
	middle-pgsql.cpp \
	middle-ram.cpp \
	node-persistent-cache.cpp \
	node-ram-cache.cpp \
	options.cpp \
	osmdata.cpp \
	output.cpp \
	output-gazetteer.cpp \
	output-multi.cpp \
	output-null.cpp \
	output-pgsql.cpp \
	parse.cpp \
	parse-o5m.cpp \
	parse-osmium.cpp \
	pgsql.cpp \
	processor-line.cpp \
	processor-point.cpp \
	processor-polygon.cpp \
	reprojection.cpp \
	sprompt.cpp \
	table.cpp \
	taginfo.cpp \
	tagtransform.cpp \
	util.cpp \
	wildcmp.cpp

nodecachefilereader_SOURCES = node-persistent-cache-reader.cpp
nodecachefilereader_LDADD = libosm2pgsql.la

check_PROGRAMS = \
	tests/test-parse-xml2 \
	tests/test-middle-ram \
	tests/test-middle-pgsql \
	tests/test-middle-flat \
	tests/test-output-multi-tags \
	tests/test-output-multi-line \
	tests/test-output-multi-line-storage \
	tests/test-output-multi-point \
	tests/test-output-multi-point-multi-table \
	tests/test-output-multi-polygon \
	tests/test-output-multi-poly-trivial \
	tests/test-output-pgsql \
	tests/test-output-pgsql-z_order \
	tests/test-output-pgsql-tablespace \
	tests/test-output-pgsql-schema \
	tests/test-pgsql-escape \
	tests/test-parse-options \
	tests/test-expire-tiles \
	tests/test-hstore-match-only

tests_test_parse_xml2_SOURCES = tests/test-parse-xml2.cpp
tests_test_parse_xml2_LDADD = libosm2pgsql.la
tests_test_middle_ram_SOURCES = tests/test-middle-ram.cpp tests/middle-tests.cpp
tests_test_middle_ram_LDADD = libosm2pgsql.la
tests_test_middle_pgsql_SOURCES = tests/test-middle-pgsql.cpp tests/middle-tests.cpp tests/common-pg.cpp
tests_test_middle_pgsql_LDADD = libosm2pgsql.la
tests_test_middle_flat_SOURCES = tests/test-middle-flat.cpp tests/middle-tests.cpp tests/common-pg.cpp
tests_test_middle_flat_LDADD = libosm2pgsql.la
tests_test_output_multi_tags_SOURCES = tests/test-output-multi-tags.cpp tests/common-pg.cpp
tests_test_output_multi_tags_LDADD = libosm2pgsql.la
tests_test_output_multi_line_SOURCES = tests/test-output-multi-line.cpp tests/common-pg.cpp
tests_test_output_multi_line_LDADD = libosm2pgsql.la
tests_test_output_multi_line_storage_SOURCES = tests/test-output-multi-line-storage.cpp tests/common-pg.cpp
tests_test_output_multi_line_storage_LDADD = libosm2pgsql.la
tests_test_output_multi_point_SOURCES = tests/test-output-multi-point.cpp tests/common-pg.cpp
tests_test_output_multi_point_LDADD = libosm2pgsql.la
tests_test_output_multi_point_multi_table_SOURCES = tests/test-output-multi-point-multi-table.cpp tests/common-pg.cpp
tests_test_output_multi_point_multi_table_LDADD = libosm2pgsql.la
tests_test_output_multi_polygon_SOURCES = tests/test-output-multi-polygon.cpp tests/common-pg.cpp
tests_test_output_multi_polygon_LDADD = libosm2pgsql.la
tests_test_hstore_match_only_SOURCES = tests/test-hstore-match-only.cpp tests/common-pg.cpp
tests_test_hstore_match_only_LDADD = libosm2pgsql.la
tests_test_output_multi_poly_trivial_SOURCES = tests/test-output-multi-poly-trivial.cpp tests/common-pg.cpp
tests_test_output_multi_poly_trivial_LDADD = libosm2pgsql.la
tests_test_output_pgsql_SOURCES = tests/test-output-pgsql.cpp tests/common-pg.cpp
tests_test_output_pgsql_LDADD = libosm2pgsql.la
tests_test_output_pgsql_tablespace_SOURCES = tests/test-output-pgsql-tablespace.cpp tests/common-pg.cpp
tests_test_output_pgsql_tablespace_LDADD = libosm2pgsql.la
tests_test_output_pgsql_z_order_SOURCES = tests/test-output-pgsql-z_order.cpp tests/common-pg.cpp
tests_test_output_pgsql_z_order_LDADD = libosm2pgsql.la
tests_test_output_pgsql_schema_SOURCES = tests/test-output-pgsql-schema.cpp tests/common-pg.cpp
tests_test_output_pgsql_schema_LDADD = libosm2pgsql.la
tests_test_pgsql_escape_SOURCES = tests/test-pgsql-escape.cpp
tests_test_pgsql_escape_LDADD = libosm2pgsql.la
tests_test_parse_options_SOURCES = tests/test-parse-options.cpp
tests_test_parse_options_LDADD = libosm2pgsql.la
tests_test_expire_tiles_SOURCES = tests/test-expire-tiles.cpp
tests_test_expire_tiles_LDADD = libosm2pgsql.la

MOSTLYCLEANFILES = tests/test_middle_flat.flat.nodes.bin tests/test_output_pgsql_area_way.flat.nodes.bin

TESTS = $(check_PROGRAMS) tests/regression-test.sh
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = sh

libosm2pgsql_la_SOURCES += fileformat.pb.cc osmformat.pb.cc

fileformat.pb.cc: contrib/protobuf/fileformat.proto
	 $(AM_V_GEN) $(PROTOC) --proto_path=contrib/protobuf --cpp_out=. $<

contrib/protobuf/osmpbf/fileformat.pb.h: fileformat.pb.cc
	cp fileformat.pb.h $@

osmformat.pb.cc: contrib/protobuf/osmformat.proto
	 $(AM_V_GEN) $(PROTOC) --proto_path=contrib/protobuf --cpp_out=. $<

contrib/protobuf/osmpbf/osmformat.pb.h: osmformat.pb.cc
	cp osmformat.pb.h $@

parse-osmium.cpp: contrib/protobuf/osmpbf/fileformat.pb.h contrib/protobuf/osmpbf/osmformat.pb.h

BUILT_SOURCES = \
  fileformat.pb.cc contrib/protobuf/osmpbf/fileformat.pb.h \
  osmformat.pb.cc contrib/protobuf/osmpbf/osmformat.pb.h

CLEANFILES = \
  fileformat.pb.cc fileformat.pb.h contrib/protobuf/osmpbf/fileformat.pb.h \
  osmformat.pb.cc osmformat.pb.h contrib/protobuf/osmpbf/osmformat.pb.h

osm2pgsqldir = $(datadir)/osm2pgsql

AM_CFLAGS = @PTHREAD_CFLAGS@ @LFS_CFLAGS@ @POSTGRESQL_CPPFLAGS@ @EXPAT_CFLAGS@ @BZIP2_CFLAGS@ @GEOS_CFLAGS@ @PROJ_CFLAGS@ @PROTOBUF_CFLAGS@ @ZLIB_CFLAGS@ -DOSM2PGSQL_DATADIR='"$(osm2pgsqldir)"' -DVERSION='"@PACKAGE_VERSION@"' @LUA_INCLUDE@
AM_CPPFLAGS = -isystem contrib/protobuf -isystem contrib/libosmium @PTHREAD_CFLAGS@ @POSTGRESQL_CPPFLAGS@ @EXPAT_CFLAGS@ @BZIP2_CFLAGS@ @GEOS_CFLAGS@ @PROJ_CFLAGS@ -DOSM2PGSQL_DATADIR='"$(osm2pgsqldir)"' -isystem geos-fallback @LUA_INCLUDE@ @BOOST_CPPFLAGS@

GLOBAL_LDFLAGS = @PTHREAD_CFLAGS@ @ZLIB_LDFLAGS@ @ZLIB_LIBS@ @POSTGRESQL_LDFLAGS@ @POSTGRESQL_LIBS@ @EXPAT_LDFLAGS@ @BZIP2_LDFLAGS@ @BZIP2_LIBS@ @GEOS_LDFLAGS@ @GEOS_LIBS@ @PROJ_LDFLAGS@ @PROJ_LIBS@ @PROTOBUF_LIBS@ -L/usr/lib/x86_64-linux-gnu @EXPAT_LIBS@ @LUA_LIB@ @BOOST_LDFLAGS@ @BOOST_FILESYSTEM_LIB@ @BOOST_SYSTEM_LIB@ @BOOST_THREAD_LIB@
osm2pgsql_LDADD += $(GLOBAL_LDFLAGS)
tests_test_parse_xml2_LDADD += $(GLOBAL_LDFLAGS)
tests_test_middle_ram_LDADD += $(GLOBAL_LDFLAGS)
tests_test_middle_pgsql_LDADD += $(GLOBAL_LDFLAGS)
tests_test_middle_flat_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_tags_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_line_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_line_storage_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_point_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_point_multi_table_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_polygon_LDADD += $(GLOBAL_LDFLAGS)
tests_test_hstore_match_only_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_multi_poly_trivial_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_pgsql_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_pgsql_tablespace_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_pgsql_z_order_LDADD += $(GLOBAL_LDFLAGS)
tests_test_output_pgsql_schema_LDADD += $(GLOBAL_LDFLAGS)
tests_test_pgsql_escape_LDADD += $(GLOBAL_LDFLAGS)
tests_test_parse_options_LDADD += $(GLOBAL_LDFLAGS)
tests_test_expire_tiles_LDADD += $(GLOBAL_LDFLAGS)
nodecachefilereader_LDADD += $(GLOBAL_LDFLAGS)

osm2pgsql_DATA = default.style 900913.sql

man1_MANS = docs/osm2pgsql.1 docs/nodecachefilereader.1

EXTRA_DIST = osm2pgsql.spec.in \
             osm2pgsql.spec \
             contrib/protobuf/fileformat.proto \
             contrib/protobuf/osmformat.proto \
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
