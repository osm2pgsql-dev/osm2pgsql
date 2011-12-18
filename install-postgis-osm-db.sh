#!/bin/sh
set -e

if [ -z $DBOWNER ]; then
    DBOWNER=gis
fi
if [ -z $DBNAME ]; then
    DBNAME=gis
fi

#    echo "Removing Old Database"
#    sudo -u postgres dropdb $DBNAME >/dev/null 2>&1 || true

    echo "Create user $DBOWNER"
    sudo -u postgres createuser --no-superuser --no-createdb --no-createrole "$DBOWNER" || true

    echo "Creating Database"
    sudo -u postgres createdb -EUTF8 -O $DBOWNER $DBNAME

    echo "Initializing Database"

    sudo -u postgres createlang plpgsql $DBNAME || true

    if [ -e /usr/share/postgresql/9.1/contrib/postgis-1.5/postgis.sql ] ; then
        echo "Initializing Spatial Extentions for postgresql 9.1"
        file_postgis=/usr/share/postgresql/9.1/contrib/postgis-1.5/postgis.sql
        file_spatial_ref=/usr/share/postgresql/9.1/contrib/postgis-1.5/spatial_ref_sys.sql
        
        sudo -u postgres psql $DBNAME <$file_postgis >/dev/null 2>&1
        sudo -u postgres psql $DBNAME <$file_spatial_ref >/dev/null 2>&1
        echo "Spatial Extentions initialized"
        
        echo "Initializing hstore"
        echo "CREATE EXTENSION hstore;" | sudo -u postgres psql $DBNAME
    else
        echo "Initializing Spatial Extentions for postgresql 8.4"
        file_postgis=/usr/share/postgresql/8.4/contrib/postgis-1.5/postgis.sql
        file_spatial_ref=/usr/share/postgresql/8.4/contrib/postgis-1.5/spatial_ref_sys.sql
        
        sudo -u postgres psql $DBNAME <$file_postgis >/dev/null 2>&1
        sudo -u postgres psql $DBNAME <$file_spatial_ref >/dev/null 2>&1
        echo "Spatial Extentions initialized"
        
        echo "Initializing hstore"
        file_hstore=/usr/share/postgresql/8.4/contrib/hstore.sql
        sudo -u postgres psql $DBNAME <$file_hstore >/dev/null 2>&1
    fi
    
    echo "Setting ownership to user $DBOWNER"

    echo 'ALTER TABLE geometry_columns OWNER TO ' $DBOWNER '; ALTER TABLE spatial_ref_sys OWNER TO ' $DBOWNER ';' | sudo -u postgres psql $DBNAME



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
	    )| sudo -u postgres psql -U postgres $DBNAME
    done
else
    echo "No extra user for postgress Database created. Please do so yourself"
fi

exit 0
