# Multi Backend #

The multi backend is designed for custom table structures as an alternative
to the standard [pgsql](pgsql.md) backend tables. It is intended to allow
the configuration of a custom set of tables with hopefully fewer rows and fewer
columns. This can be beneficial to queries in which some context (eg. zoom level)
could limit the number of tables that need to be queried. Addtionaly it would
allow more tables to be queried in parallel. 

## Database Layout ##
It connects to a PostgreSQL database and stores the data in one or more tables.
As sample configuration may resemble the following:

    {
      [
        {
          "name": "building",
          "type": "polygon",
          "tagtransform": "building.lua",
          "tagtransform-way-function": "ways_proc",
          "tagtransform-relation-function": "relation_proc",
          "tagtransform-relation-member-function": "relation_member_proc",
          "tags": [
            {"name": "building", "type": "text"},
            {"name": "shop", "type": "text"},
            {"name": "amenity", "type": "text"}
          ]
        }
      ]
    }


Note that each table has a `name` and can target a single type of geometry
by setting the `type` to one of `point`, `line` or `polygon`. Beyond that
each table is configured in a way similar to that of the `pgsql` backend.
That is essentially why it was named `multi` because it's basically multiple
`pgsql` backends each with its own set of options and only a single table.

TODO: detail the rest of the multi options.

## Importing ##

See: [Importing](pgsql.md#Importing).
