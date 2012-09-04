#!/bin/bash
set -e

trap errorhandler ERR

errorhandler(){
    echo "!!!!!!TEST failed, please check results!!!!!!"
    exit $status
}

planetfile=$1
planetdiff=$2
test_output=`dirname  $0`/test_output_$$

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
    rm -f $test_output $test_output.*
    dropdb osm2pgsql-test

}

function psql_test {
    ( echo -n "$1"; psql -c "$2" -t -d osm2pgsql-test ) | tee -a $test_output.tmp
}

function reset_results {
    rm -f $test_output $test_output.*
}

function compare_results {
    if [ ! -r $test_output ]; then
        mv $test_output.tmp $test_output
    elif diff $test_output $test_output.tmp >/dev/null; then
        rm $test_output.tmp
    else
        errorhandler
    fi
}

function test_osm2pgsql_slim {
    trap errorhandler ERR
    echo ""
    echo ""
    echo "@@@Testing osm2pgsql in slim mode with the following parameters: \"" $1 "\"@@@"
    setup_db

    dbprefix=${2:-planet_osm}

    ./osm2pgsql --slim --create -d osm2pgsql-test $1 $planetfile
    psql_test "Number of points imported" "SELECT count(*) FROM ${dbprefix}_point;"
    psql_test "Number of lines imported" "SELECT count(*) FROM ${dbprefix}_line;"
    psql_test "Number of roads imported" "SELECT count(*) FROM ${dbprefix}_roads;"
    psql_test "Number of polygon imported" "SELECT count(*) FROM ${dbprefix}_polygon;"
    psql_test "Number of nodes imported" "SELECT count(*) FROM ${dbprefix}_nodes;"
    psql_test "Number of ways imported" "SELECT count(*) FROM ${dbprefix}_ways;"
    psql_test "Number of relations imported" "SELECT count(*) FROM ${dbprefix}_rels;"

    echo "***Testing osm2pgsql diff import with the following parameters: \"" $1 "\"***"
    ./osm2pgsql --slim --append -d osm2pgsql-test $1 $planetdiff
    psql_test "Number of points imported" "SELECT count(*) FROM ${dbprefix}_point;"
    psql_test "Number of lines imported" "SELECT count(*) FROM ${dbprefix}_line;"
    psql_test "Number of roads imported" "SELECT count(*) FROM ${dbprefix}_roads;"
    psql_test "Number of polygon imported" "SELECT count(*) FROM ${dbprefix}_polygon;"
    psql_test "Number of nodes imported" "SELECT count(*) FROM ${dbprefix}_nodes;"
    psql_test "Number of ways imported" "SELECT count(*) FROM ${dbprefix}_ways;"
    psql_test "Number of relations imported" "SELECT count(*) FROM ${dbprefix}_rels;"
    compare_results
}

function test_osm2pgsql_gazetteer {
    trap errorhandler ERR
    echo ""
    echo ""
    echo "@@@Testing osm2pgsql in gazetteer mode with the following parameters: \"" $1 "\"@@@"
    setup_db

    dbprefix=${2:-planet_osm}

    ./osm2pgsql --slim --create -l -O gazetteer -d osm2pgsql-test $1 $planetfile
    psql_test "Number of places imported" "SELECT count(*) FROM place;"
    psql_test "Number of nodes imported" "SELECT count(*) FROM ${dbprefix}_nodes;"
    psql_test "Number of ways imported" "SELECT count(*) FROM ${dbprefix}_ways;"
    psql_test "Number of relations imported" "SELECT count(*) FROM ${dbprefix}_rels;"

    echo "***Testing osm2pgsql diff import with the following parameters: \"" $1 "\"***"
    ./osm2pgsql --slim --append -l -O gazetteer -d osm2pgsql-test $1 $planetdiff
    psql_test "Number of places imported" "SELECT count(*) FROM place;"
    psql_test "Number of nodes imported" "SELECT count(*) FROM ${dbprefix}_nodes;"
    psql_test "Number of ways imported" "SELECT count(*) FROM ${dbprefix}_ways;"
    psql_test "Number of relations imported" "SELECT count(*) FROM ${dbprefix}_rels;"
    compare_results
}

function test_osm2pgsql_nonslim {
    trap errorhandler ERR
    echo ""
    echo ""
    echo "@@@Testing osm2pgsql with the following parameters: \"" $1 "\"@@@"
    setup_db
    ./osm2pgsql --create -d osm2pgsql-test $1 $planetfile
    psql_test "Number of points imported" "SELECT count(*) FROM planet_osm_point;"
    psql_test "Number of lines imported" "SELECT count(*) FROM planet_osm_line;"
    psql_test "Number of roads imported" "SELECT count(*) FROM planet_osm_roads;"
    psql_test "Number of polygon imported" "SELECT count(*) FROM planet_osm_polygon;"
    compare_results
}


test_osm2pgsql_nonslim "-S default.style -C 100"
test_osm2pgsql_nonslim "-S default.style -C 100"
echo ========== OK SO FAR =============
test_osm2pgsql_nonslim "-S default.style -l -C 100"
test_osm2pgsql_nonslim "--slim --drop -S default.style -C 100"
reset_results

echo ========== NOW DOING SLIM =============
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
reset_results

#test_osm2pgsql_gazetteer "-C 100"
#test_osm2pgsql_gazetteer "--bbox -90.0,-180.0,90.0,180.0 -C 100"

teardown_db



