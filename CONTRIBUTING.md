# Osm2pgsql contribution guidelines

## Workflow

We operate the "Fork & Pull" model explained at

https://help.github.com/articles/using-pull-requests

You should fork the project into your own repo, create a topic branch
there and then make one or more pull requests back to the openstreetmap repository.
Your pull requests will then be reviewed and discussed.

## History

To understand the osm2pgsql code, it helps to know some history on it. Osm2pgsql
was written in C in 2007 as a port of an older Python utility. In 2014 it was
ported to C++ by MapQuest and the last C version was released as 0.86.0. In it's
time, it has had varying contribution activity, including times with no
maintainer or active developers.

Parts of the codebase still clearly show their C origin and could use rewriting
in modern C++, making use of data structures in the standard library.

## Versioning

Osm2pgsql uses a X.Y.Z version number, where Y tells you if you are on a stable
or development series. Like the Linux Kernel, even numbers are stable and
development versions are odd.

Bugs and known issues are fixed on the main branch only. Exceptions may be made
for easy bug fixes, or if a patch backporting a fix is provided.

## Code style

Code must be written in the
[K&R 1TBS style](https://en.wikipedia.org/wiki/Indent_style#Variant:_1TBS) with
4 spaces indentation. Tabs should never be used in the C++ code. Braces must
always be used for code blocks, even one-liners.

Names should use underscores, not camel case, with class/struct names ending in `_t`.
Template parameters must use all upper case.

Headers should be included in the order `config.h`, C++ standard library headers,
C library headers, Boost headers, and last osm2pgsql files.

There is a .clang-format configuration avialable and all code must be run through
clang-format before submitting. You can use git-clang-format after staging all
your changes:

    git-clang-format --style=file *pp tests/*pp

clang-format 3.8 or later is required.

## Documentation

User documentation is stored in `docs/`. Pages on the OpenStreetMap wiki are
known to be unreliable and outdated.

There is some documentation in Doxygen-formatted comments. The documentation can
be generated with ``doxygen docs/Doxyfile``. It is not yet hooked into the build
scripts as most functions are not yet documented.

## Platforms targeted

Ideally osm2pgsql should compile on Linux, OS X, FreeBSD and Windows. It is
actively tested on Debian, Ubuntu and FreeBSD by the maintainers.

## Testing

The code also comes with a suite of tests which can be run by
executing ``ctest``.

Regression tests require python and psycopg to be installed. On Ubuntu run:

```sh
sudo apt-get install python3-psycopg2
```

Most of these tests depend on being able to set up a database and run osm2pgsql
against it. This is most easily done using ``pg_virtualenv``. Just run

```sh
pg_virtualenv ctest
```

``pg_virtualenv`` creates a separate postgres server instance. The test databases
are created in this instance and the complete server is destroyed after the
tests are finished. ctest also calls appropriate fixtures that create the
separate tablespace required for some tests.

When running without ``pg_virtualenv``, you need to ensure that PostgreSQL is
running and that your user is a superuser of that system. You also need to
create an appropriate test tablespace manually. To do that, run:

```sh
sudo -u postgres createuser -s $USER
sudo mkdir -p /tmp/psql-tablespace
sudo chown postgres.postgres /tmp/psql-tablespace
psql -c "CREATE TABLESPACE tablespacetest LOCATION '/tmp/psql-tablespace'" postgres
```

Once this is all set up, all the tests should run (no SKIPs), and pass
(no FAILs). If you encounter a failure, you can find more information
by looking in the `test-suite.log`. If you find something which seems
to be a bug, please check to see if it is a known issue at
https://github.com/openstreetmap/osm2pgsql/issues and, if it's not
already known, report it there.

### Performance Testing

If performance testing with a full planet import is required, indicate what
needs testing in a pull request.

## Maintainers

The current maintainers of osm2pgsql are [Sarah Hoffmann](https://github.com/lonvia/)
and [Paul Norman](https://github.com/pnorman/). Sarah has more experience with
the gazetteer backend and Paul with the pgsql and multi backends.
