#!/bin/bash
set -e

trap errorhandler ERR

errorhandler(){
    echo "!!!!!!TEST failed, please check results!!!!!!"
    exit $status
}

planetfile=$1
planetdiff=$2

function setup_db {
    echo ""
    echo "Initialising test db"
    dropdb osm2pgsql-test > /dev/null || true
    createdb -E UTF8 osm2pgsql-test
    psql -f /usr/share/postgresql/9.1/contrib/postgis-1.5/postgis.sql -d osm2pgsql-test > /dev/null
    psql -f /usr/share/postgresql/9.1/contrib/postgis-1.5/spatial_ref_sys.sql -d osm2pgsql-test > /dev/null
    psql -c "CREATE EXTENSION hstore;" -d osm2pgsql-test &> /dev/null
    sudo rm -rf /tmp/psql-tablespace || true
    mkdir /tmp/psql-tablespace
    sudo chown postgres.postgres /tmp/psql-tablespace
    psql -q -c "DROP TABLESPACE tablespacetest" -d osm2pgsql-test > /dev/null || true
    psql -c "CREATE TABLESPACE tablespacetest LOCATION '/tmp/psql-tablespace'" -d osm2pgsql-test
}

function teardown_db {
    dropdb osm2pgsql-test #To remove any objects that might still be in the table space
    psql -c "DROP TABLESPACE tablespacetest" -d postgres
    sudo rm -rf /tmp/psql-tablespace
    dropdb osm2pgsql-test

}

function test_osm2pgsql_slim {
    trap errorhandler ERR
    echo ""
    echo ""
    echo "@@@Testing osm2pgsql in slim mode with the following parameters: \"" $1 "\"@@@"
    setup_db

    dbprefix=${2:-planet_osm}

    ./osm2pgsql --slim --create -d osm2pgsql-test $1 $planetfile
    echo -n "Number of points imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_point;" -t -d osm2pgsql-test || true
    echo -n "Number of lines imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_line;" -t -d osm2pgsql-test || true
    echo -n "Number of roads imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_roads;" -t -d osm2pgsql-test || true
    echo -n "Number of polygon imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_polygon;" -t -d osm2pgsql-test || true
    echo -n "Number of nodes imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_nodes;" -t -d osm2pgsql-test || true
    echo -n "Number of ways imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_ways;" -t -d osm2pgsql-test || true
    echo -n "Number of relations imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_rels;" -t -d osm2pgsql-test || true

    echo "***Testing osm2pgsql diff import with the following parameters: \"" $1 "\"***"
    ./osm2pgsql --slim --append -d osm2pgsql-test $1 $planetdiff
    echo -n "Number of points imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_point;" -t -d osm2pgsql-test || true
    echo -n "Number of lines imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_line;" -t -d osm2pgsql-test || true
    echo -n "Number of roads imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_roads;" -t -d osm2pgsql-test || true
    echo -n "Number of polygon imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_polygon;" -t -d osm2pgsql-test || true
    echo -n "Number of nodes imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_nodes;" -t -d osm2pgsql-test || true
    echo -n "Number of ways imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_ways;" -t -d osm2pgsql-test || true
    echo -n "Number of relations imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_rels;" -t -d osm2pgsql-test || true
}
function test_osm2pgsql_gazetteer {
    trap errorhandler ERR
    echo ""
    echo ""
    echo "@@@Testing osm2pgsql in gazetteer mode with the following parameters: \"" $1 "\"@@@"
    setup_db

    dbprefix=${2:-planet_osm}

    ./osm2pgsql --slim --create -l -O gazetteer -d osm2pgsql-test $1 $planetfile
    echo -n "Number of places imported"
    psql -c "SELECT count(*) FROM place;" -t -d osm2pgsql-test || true
    echo -n "Number of nodes imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_nodes;" -t -d osm2pgsql-test || true
    echo -n "Number of ways imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_ways;" -t -d osm2pgsql-test || true
    echo -n "Number of relations imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_rels;" -t -d osm2pgsql-test || true

    echo "***Testing osm2pgsql diff import with the following parameters: \"" $1 "\"***"
    ./osm2pgsql --slim --append -l -O gazetteer -d osm2pgsql-test $1 $planetdiff
    echo -n "Number of places imported"
    psql -c "SELECT count(*) FROM place;" -t -d osm2pgsql-test || true
    echo -n "Number of nodes imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_nodes;" -t -d osm2pgsql-test || true
    echo -n "Number of ways imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_ways;" -t -d osm2pgsql-test || true
    echo -n "Number of relations imported"
    psql -c "SELECT count(*) FROM ${dbprefix}_rels;" -t -d osm2pgsql-test || true
}

function test_osm2pgsql_nonslim {
    trap errorhandler ERR
    echo ""
    echo ""
    echo "@@@Testing osm2pgsql with the following parameters: \"" $1 "\"@@@"
    setup_db
    ./osm2pgsql --create -d osm2pgsql-test $1 $planetfile
    echo -n "Number of points imported"
    psql -c "SELECT count(*) FROM planet_osm_point;" -t -d osm2pgsql-test
    echo -n "Number of lines imported"
    psql -c "SELECT count(*) FROM planet_osm_line;" -t -d osm2pgsql-test
    echo -n "Number of roads imported"
    psql -c "SELECT count(*) FROM planet_osm_roads;" -t -d osm2pgsql-test
    echo -n "Number of polygon imported"
    psql -c "SELECT count(*) FROM planet_osm_polygon;" -t -d osm2pgsql-test
}


test_osm2pgsql_nonslim "-S default.style -C 100"
test_osm2pgsql_nonslim "-S default.style -l -C 100"
test_osm2pgsql_nonslim "--slim --drop -S default.style -C 100"
test_osm2pgsql_slim "-S default.style -C 100"
test_osm2pgsql_slim "-S default.style -l -C 100"
test_osm2pgsql_slim "-k -S default.style -C 100"
test_osm2pgsql_slim "-j -S default.style -C 100"
test_osm2pgsql_slim "-K -S default.style -C 100"
test_osm2pgsql_slim "-x -S default.style -C 100"
test_osm2pgsql_slim "-p planet_osm2 -S default.style -C 100" "planet_osm2"
test_osm2pgsql_slim "--bbox -90.0,-180.0,90.0,180.0 -S default.style -C 100"
test_osm2pgsql_slim "--number-processes 6 -S default.style -C 100"
test_osm2pgsql_slim "-I -S default.style -C 100"
test_osm2pgsql_slim "-e 16:16 -S default.style -C 100"
test_osm2pgsql_slim "--number-processes 6 -e 16:16 -S default.style -C 100"
test_osm2pgsql_slim "-S default.style -C 100 -i tablespacetest"
test_osm2pgsql_slim "-S default.style -C 100 --tablespace-main-data tablespacetest"
test_osm2pgsql_slim "-S default.style -C 100 --tablespace-main-index tablespacetest"
test_osm2pgsql_slim "-S default.style -C 100 --tablespace-slim-data tablespacetest"
test_osm2pgsql_slim "-S default.style -C 100 --tablespace-slim-index tablespacetest"

test_osm2pgsql_gazetteer "-C 100"
test_osm2pgsql_gazetteer "--bbox -90.0,-180.0,90.0,180.0 -C 100"

teardown_db



