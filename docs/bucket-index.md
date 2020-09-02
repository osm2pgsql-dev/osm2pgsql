
# Bucket index for slim mode

Since version XXX osm2pgsql can use an index for way node lookups in slim mode
that needs a lot less disk space than before. For a planet the savings can be
about 200 GB! Lookup times are slightly slower, but this shouldn't be an issue
for most people.

If you are not using slim mode and/or not doing updates of your database, this
does not apply to you.

For backwards compatibility osm2pgsql will **not** update an existing database
to the new index. It will keep using the old index. So you do not have to do
anything when upgrading osm2pgsql.

If you want to use the new index, there are two ways of doing this: The "safe"
way for most users and the "doit-it-yourself" way for expert users. Note that
once you switched to the new index, older versions of osm2pgsql will not work
correctly any more.

## Update for most users

If your database was created with an older version of osm2pgsql you might want
to start again from an empty database. Just do a reimport and osm2pgsql will
use the new space-saving index.

## Update for expert users

This is only for users who are very familiar with osm2pgsql and PostgreSQL
operation. You can break your osm2pgsql database beyond repair if something
goes wrong here and you might not even notice.

You can create the index yourself by following these steps:

Drop the existing index. Replace `{prefix}` by the prefix you are using.
Usually this is `planet_osm`:

```
DROP INDEX {prefix}_ways_nodes_idx;
```

Create the `index_bucket` function needed for the index. Replace
`{index_bucket_size}` by the bucket size you want. If you don't have a reason
to use something else, use `32`:

```
CREATE FUNCTION {prefix}_index_bucket(int8[]) RETURNS int8[] AS $$
  SELECT ARRAY(SELECT DISTINCT unnest($1)/{index_bucket_size})
$$ LANGUAGE SQL IMMUTABLE;
```

Now you can create the new index. Again, replace `{prefix}` by the prefix
you are using:

```
CREATE INDEX {prefix}_ways_index_bucket_idx ON {prefix}_ways
  USING GIN ({prefix}_index_bucket(nodes))
  WITH (fastupdate = off);
```

If you want to create the index in a specific tablespace you can do this:

```
CREATE INDEX {prefix}_ways_index_bucket_idx ON {prefix}_ways
  USING GIN ({prefix}_index_bucket(nodes))
  WITH (fastupdate = off) TABLESPACE {tablespace};
```

## Bucket size (for experts)

When creating a new database (when used in create mode with slim option),
osm2pgsql will create a bucket index using bucket size 32.

You can set the environment variable `OSM2PGSQL_INDEX_BUCKET_SIZE` to the
bucket size you want. Values between about 8 and 64 might make sense.

To completely disable the bucket index and create an index compatible with
earlier versions of osm2pgsql, set `OSM2PGSQL_INDEX_BUCKET_SIZE` to `0`.

