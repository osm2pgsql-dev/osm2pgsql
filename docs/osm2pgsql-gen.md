# NAME

osm2pgsql-gen - Generalize OpenStreetMap data - EXPERIMENTAL!

# SYNOPSIS

**osm2pgsql-gen** \[*OPTIONS*\]...

# DESCRIPTION

THIS PROGRAM IS EXPERIMENTAL AND MIGHT CHANGE WITHOUT NOTICE!

**osm2pgsql-gen** reads data imported by **osm2pgsql** from the database,
performs various generalization steps specified by a Lua config file and
writes the data back to the database. It is used in conjunction with and
after **osm2pgsql** and reads the same config file.

This man page can only cover some of the basics and describe the command line
options. See the [Generalization chapter in the osm2pgsql
Manual](https://osm2pgsql.org/doc/manual.html#generalization) for more
information.

# OPTIONS

This program follows the usual GNU command line syntax, with long options
starting with two dashes (`--`). Mandatory arguments to long options are
mandatory for short options too.

# MAIN OPTIONS

-a, \--append
:   Run in append mode.

-c, \--create
:   Run in create mode. This is the default if **-a, \--append** is not
    specified.

-S, \--style=FILE
:   The Lua config file. Same as for **osm2pgsql**.

-j, \-jobs=NUM
:   Specifies the number of parallel threads used for certain operations.
    Setting this to the number of available CPU cores is a reasonable starting
    point.

# HELP/VERSION OPTIONS

-h, \--help
:   Print help.

-V, \--version
:   Print osm2pgsql version.

# LOGGING OPTIONS

\--log-level=LEVEL
:   Set log level ('debug', 'info' (default), 'warn', or 'error').

\--log-sql
:   Enable logging of SQL commands for debugging.

# DATABASE OPTIONS

-d, \--database=NAME
:   The name of the PostgreSQL database to connect to. If this parameter
    contains an `=` sign or starts with a valid URI prefix (`postgresql://` or
    `postgres://`), it is treated as a conninfo string. See the PostgreSQL
    manual for details.

-U, \--username=NAME
:   Postgresql user name.

-W, \--password
:   Force password prompt.

-H, \--host=HOSTNAME
:   Database server hostname or unix domain socket location.

-P, \--port=PORT
:   Database server port.

# SEE ALSO

* [osm2pgsql website](https://osm2pgsql.org)
* [osm2pgsql manual](https://osm2pgsql.org/doc/manual.html)
* **postgres**(1)
* **osm2pgsql**(1)

