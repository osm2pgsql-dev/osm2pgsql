# the Makefile template provided by 'pg_config --pgxs' does not
# play well with autotools, so we're ignoring it here 

pkglib_LTLIBRARIES = gazetteer.la
gazetteer_la_SOURCES = gazetteer.c

gazetteer_la_CFLAGS = @POSTGRESQL_SERVER_CFLAGS@ $(AM_CFLAGS)
gazetteer_la_LDFLAGS = -module -avoid-version -shared $(AM_LDFLAGS)

noinst_HEADERS = utfasciitable.h tokenstringreplacements.inc
