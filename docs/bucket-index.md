
NOTE: This is only available from osm2pgsql version 1.4.0!

NOTE: The default is still to create the old index for now.

# Bucket index for slim mode

Osm2pgsql can use an index for way node lookups in slim mode that needs a lot
less disk space than earlier versions did. For a planet the savings can be
about 200 GB! Lookup times are slightly slower, but this shouldn't be an issue
for most people.

*If you are not using slim mode and/or not doing updates of your database, this
does not apply to you.*

For backwards compatibility osm2pgsql will never update an existing database
to the new index. It will keep using the old index. So you do not have to do
anything when upgrading osm2pgsql.

If you want to use the new index, there are two ways of doing this: The "safe"
way for most users and the "doit-it-yourself" way for expert users. Note that
once you switched to the new index, older versions of osm2pgsql will not work
correctly any more.

## Update for most users

NOTE: This does not work yet. Currently the default is still to create the
old type of index.

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
`{way_node_index_id_shift}` by the number of bits you want the id to be
shifted. If you don't have a reason to use something else, use `5`:

```
CREATE FUNCTION {prefix}_index_bucket(int8[]) RETURNS int8[] AS $$
  SELECT ARRAY(SELECT DISTINCT unnest($1) >> {way_node_index_id_shift})
$$ LANGUAGE SQL IMMUTABLE;
```

Now you can create the new index. Again, replace `{prefix}` by the prefix
you are using:

```
CREATE INDEX {prefix}_ways_nodes_bucket_idx ON {prefix}_ways
  USING GIN ({prefix}_index_bucket(nodes))
  WITH (fastupdate = off);
```

If you want to create the index in a specific tablespace you can do this:

```
CREATE INDEX {prefix}_ways_nodes_bucket_idx ON {prefix}_ways
  USING GIN ({prefix}_index_bucket(nodes))
  WITH (fastupdate = off) TABLESPACE {tablespace};
```

## Id shift (for experts)

When an OSM node is changing, the way node index is used to look up all ways
that use that particular node and therefore might have to be updated, too.
This index is quite large, because most nodes are in at least one way.

When creating a new database (when used in create mode with slim option),
osm2pgsql can create a "bucket index" using a configurable id shift for the
nodes in the way node index. This bucket index will create index entries not
for each node id, but for "buckets" of node ids. It does this by shifting the
node ids a few bits to the right. As a result there are far fewer entries
in the index, it becomes a lot smaller. This is especially true in our case,
because ways often contain consecutive nodes, so if node id `n` is in a way,
there is a good chance, that node id `n+1` is also in the way.

On the other hand, looking up an id will result in false positives, so the
database has to retrieve more ways than absolutely necessary, which leads to
the considerable slowdown.

You can set the shift with the command line option
`--middle-way-node-index-id-shift`. Values between about 3 and 6 might make
sense.

To completely disable the bucket index and create an index compatible with
earlier versions of osm2pgsql, use `--middle-way-node-index-id-shift=0`.
(This is currently still the default.)

