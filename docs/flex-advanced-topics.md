
# Advanced Topics for the Flex Backend

The [flex backend](flex.md) allows a wide range of configuration options. Here
are some extra tips & tricks.

## Primary Keys and Unique IDs

It is often desirable to have a unique PRIMARY KEY on database tables. Many
programs need this.

There seems to be a natural unique key, the OSM node, way, or relation ID the
data came from. But there is a problem with that: Depending on the
configuration, osm2pgsql sometimes adds several rows to the output tables with
the same OSM ID. This typically happens when long linestrings are split into
shorter pieces or multipolygons are split into their constituent polygons, but
it can also happen if your Lua configuration file adds two rows to the same
table.

If you need unique keys on your database tables there are two options: Using
those natural keys and making sure that you don't have duplicate entries. Or
adding an additional ID column. The latter is easier to do and will work in
all cases, but it adds some overhead.

### Using Natural Keys

To use OSM IDs as primary keys, you have to make sure that

* You only ever add a single row per OSM object to an output table, i.e. do
  not call `add_row` multiple times on the same table for the same OSM object.
* osm2pgsql doesn't split long linestrings into smaller ones. So you can not
  use the `split_at` option on the geometry transformation.
* osm2pgsql doesn't split multipolygons into polygons. So you have to set
  `multi = true` on all `area` geometry transformations.

You probably also want an index on the ID column. If you are running in slim
mode, osm2pgsql will create that index for you. But in non-slim mode you have
to do this yourself with `CREATE UNIQUE INDEX`. You can also use `ALTER TABLE`
to make the column an "official" primary key column. See the PostgreSQL docs
for details.

### Using an Additional ID Column

PostgreSQL has the somewhat magic
["serial" data types](https://www.postgresql.org/docs/12/datatype-numeric.html#DATATYPE-SERIAL).
If you use that datatype in a column definition, PostgreSQL will add an
integer column to the table and automatically fill it with an autoincrementing
value.

In the flex config you can add such a column to your tables with something
like this:

```
...
{ column = 'id', type = 'serial', create_only = true },
...
```

The `create_only` tells osm2pgsql that it should create this column but not
try to fill it when adding rows (because PostgreSQL does it for us).

You probably also want an index on this column. After the first import of your
data using osm2pgsql, use `CREATE UNIQUE INDEX` to create one. You can also use
`ALTER TABLE` to make the column an "official" primary key column. See the
PostgreSQL docs for details.

## Using `create_only` columns for postprocessed data

Sometimes it is useful to have data in table rows that osm2pgsql can't create.
For instance you might want to store the center of polygons for faster
rendering of a label.

To do this define your table as usual and add an additional column, marking
it `create_only`. In our example case the type of the column should be the
PostgreSQL type `GEOMETRY(Point, 3857)`, because we don't want osm2pgsql to
do anything special here, just create the column with this type as is.

```
polygons_table = osm2pgsql.define_area_table('polygons', {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'geometry' },
    { column = 'center', type = 'GEOMETRY(Point, 3857)', create_only = true },
    { column = 'area', type = 'area' },
})
```

After running osm2pgsql as usual, run the following SQL command:

```
UPDATE polygons SET center = ST_Centroid(geom) WHERE center IS NULL;
```

If you are updating the data using `osm2pgsql --append`, you have to do this
after each update. When osm2pgsql inserts new rows they will always have a
`NULL` value in `center`, the `WHERE` condition makes sure that we only do
this (possibly expensive) calculation once.

