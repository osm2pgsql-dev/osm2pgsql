#!/bin/bash
set -e

for tablespace in tablespacetest osmtest ; do
	mkdir -pv "/var/lib/postgresql/data/$tablespace"
	psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" -c "CREATE TABLESPACE $tablespace LOCATION '/var/lib/postgresql/data/$tablespace'"
done

