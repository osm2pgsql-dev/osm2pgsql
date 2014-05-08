ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = osm2pgsql nodecachefilereader
noinst_LTLIBRARIES = libosm2pgsql.la

osm2pgsql_SOURCES = osm2pgsql.c \
	binarysearcharray.h \
	build_geometry.h \
	expire-tiles.h \
	input.h \
	keyvals.h \
	middle-pgsql.h \
	middle-ram.h \
	middle.h \
	node-persistent-cache.h \
	node-ram-cache.h \
	osmtypes.h \
	output-gazetteer.h \
	output-null.h \
	output-pgsql.h \
	output.h \
	parse-o5m.h \
	parse-primitive.h \
	parse-xml2.h \
	pgsql.h \
	rb.h \
	reprojection.h \
	sanitizer.h \
	sprompt.h \
	text-tree.h

osm2pgsql_LDADD = libosm2pgsql.la

libosm2pgsql_la_SOURCES = \
	UTF8sanitizer.c \
	binarysearcharray.c \
	build_geometry.cpp \
	expire-tiles.c \
	input.c \
	keyvals.c \
	middle-pgsql.c \
	middle-ram.c \
	node-persistent-cache.c \
	node-ram-cache.c \
	osmtypes.c \
	output-gazetteer.c \
	output-null.c \
	output-pgsql.c \
	parse-o5m.c \
	parse-primitive.c \
	parse-xml2.c \
	pgsql.c \
	rb.c \
	reprojection.c \
	sprompt.c \
	tagtransform.c \
	text-tree.c \
	wildcmp.c

nodecachefilereader_SOURCES = node-persistent-cache-reader.c
nodecachefilereader_LDADD = libosm2pgsql.la

check_PROGRAMS = \
	tests/test-parse-xml2 \
	tests/test-middle-ram

tests_test_parse_xml2_SOURCES = tests/test-parse-xml2.cpp 
tests_test_parse_xml2_LDADD = libosm2pgsql.la
tests_test_middle_ram_SOURCES = tests/test-middle-ram.cpp tests/middle-tests.cpp
tests_test_middle_ram_LDADD = libosm2pgsql.la

TESTS = $(check_PROGRAMS) tests/regression-test.sh
TEST_EXTENSIONS = .sh
SH_LOG_COMPILER = sh

if READER_PBF
osm2pgsql_SOURCES += parse-pbf.h fileformat.pb-c.h osmformat.pb-c.h
libosm2pgsql_la_SOURCES += parse-pbf.c fileformat.pb-c.c osmformat.pb-c.c

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
AM_CPPFLAGS = @PTHREAD_CFLAGS@ @POSTGRESQL_CFLAGS@ @XML2_CFLAGS@ @BZIP2_CFLAGS@ @GEOS_CFLAGS@ @PROJ_CFLAGS@ -DOSM2PGSQL_DATADIR='"$(osm2pgsqldir)"' -Igeos-fallback @LUA_INCLUDE@

GLOBAL_LDFLAGS = @PTHREAD_CFLAGS@ @ZLIB_LDFLAGS@ @ZLIB_LIBS@ @POSTGRESQL_LDFLAGS@ @POSTGRESQL_LIBS@ @XML2_LDFLAGS@ @BZIP2_LDFLAGS@ @BZIP2_LIBS@ @GEOS_LDFLAGS@ @GEOS_LIBS@ @PROJ_LDFLAGS@ @PROJ_LIBS@ @PROTOBUF_C_LDFLAGS@ @PROTOBUF_C_LIBS@ -L/usr/lib/x86_64-linux-gnu @LUA_LIB@ -lstdc++
osm2pgsql_LDADD += $(GLOBAL_LDFLAGS)
tests_test_parse_xml2_LDADD += $(GLOBAL_LDFLAGS)
tests_test_middle_ram_LDADD += $(GLOBAL_LDFLAGS)
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
