# the Makefile template provided by 'pg_config --pgxs' does not
# play well with autotools, so we're ignoring it here 

pkglib_LTLIBRARIES = gazetteer.la
gazetteer_la_SOURCES = gazetteer.c

gazetteer_la_CFLAGS = @POSTGRESQL_SERVER_CFLAGS@ $(AM_CFLAGS)
gazetteer_la_LDFLAGS = -module -avoid-version -shared $(AM_LDFLAGS)

gazetteerdir = $(datadir)/gazetteer

gazetteer_SQL = extract_countrynames.sql gazetteer-index.sql gazetteer-loaddata.sql gazetteer-tables.sql import_country_name.sql import_country_osm_grid.sql import_gb_postcodearea.sql import_gb_postcode.sql import_specialwords.sql import_us_statecounty.sql import_us_state.sql import_worldboundaries.sql

gazetteer_DATA = $(gazetteer_SQL) gazetteer-functions.sql

EXTRA_DIST = $(gazetteer_SQL) gazetteer-functions.sql.in

noinst_HEADERS = utfasciitable.h tokenstringreplacements.inc

gazetteer-functions.sql: gazetteer-functions.sql.in
	sed -e"s/gazetteerdir/"`echo $(gazetteerdir) | sed -e's/\//\\\\\//g'`"/g" < $< > $@