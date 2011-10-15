#!/bin/sh
set -e

if [ $# -ne 2 ] ; then
    echo "Usage: install-postgis-osm-user.sh DBNAME USERNAME"
    exit
fi

DBNAME=$1
GRANT_USER=$2


if [ -n "$GRANT_USER" ] ; then

    if [ "$GRANT_USER" = "*" ] ; then
	echo "GRANT Rights to every USER"
	GRANT_USER=''
	for user in `users` ; do 
	    GRANT_USER="$GRANT_USER $user"
	done
    fi

    for user in $GRANT_USER; do
	    sudo -u postgres createuser --no-superuser --no-createdb --no-createrole "$user" || true
	    echo "Granting rights to user '$user'"
	    (
	        echo "GRANT ALL on geometry_columns TO \"$user\";"
	        echo "GRANT ALL ON SCHEMA PUBLIC TO \"$user\";"
	        echo "GRANT ALL on spatial_ref_sys TO \"$user\";"
            echo "GRANT ALL on planet_osm_line TO \"$user\";"
            echo "GRANT ALL on planet_osm_nodes TO \"$user\";"
            echo "GRANT ALL on planet_osm_point TO \"$user\";"
            echo "GRANT ALL on planet_osm_rels TO \"$user\";"
            echo "GRANT ALL on planet_osm_roads TO \"$user\";"
            echo "GRANT ALL on planet_osm_ways TO \"$user\";"
            echo "GRANT ALL on planet_osm_polygon TO \"$user\";"
	    )| sudo -u postgres psql -Upostgres $DBNAME
    done
else
    echo "No extra user for postgress Database created. Please do so yourself"
fi

exit 0