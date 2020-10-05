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

Osm2pgsql uses [semantic versioning](https://semver.org/).

Bugs and known issues are fixed on the main branch only. Exceptions may be made
for severe bugs.

## Code style

Code must be written in the
[K&R 1TBS style](https://en.wikipedia.org/wiki/Indent_style#Variant:_1TBS) with
4 spaces indentation. Tabs should never be used in the C++ code. Braces must
always be used for code blocks, even one-liners.

Names should use underscores, not camel case, with class/struct names ending in `_t`.
Template parameters must use all upper case.

Headers should be included in the order `config.h`, C++ standard library headers,
C library headers, Boost headers, and last osm2pgsql files.

There is a .clang-format configuration available and all code must be run through
clang-format before submitting. You can use git-clang-format after staging all
your changes:

    git-clang-format src/*pp tests/*pp

clang-format 7 or later is required.

Comments in code should follow the [Doxygen
convention](https://www.doxygen.nl/manual/docblocks.html) using backslashes
(not @-signs) for commands.

## Documentation

User documentation is available on [the website](https://osm2pgsql.org/), some
is stored in `docs/`. Pages on the OpenStreetMap wiki are known to be
unreliable and outdated.

The [man page](docs/osm2pgsql.1) can be built from [source](docs/osm2pgsql.md)
with `make man`. The result should be checked into the repository.

## Platforms targeted

Ideally osm2pgsql should compile on Linux, OS X, FreeBSD and Windows. It is
actively tested on Debian, Ubuntu and FreeBSD by the maintainers.

## Testing

The code comes with a suite of tests. They are only compiled and run when
`BUILD_TESTS=ON` is set in the CMake config.

Tests are executed by calling `ctest`. You can call `ctest` with `-L NoDB` to
only run tests that don't need a database.

Regression tests require python and psycopg to be installed. On Ubuntu run:

```sh
sudo apt-get install python3-psycopg2
```

Most of these tests depend on being able to set up a database and run osm2pgsql
against it. This is most easily done using `pg_virtualenv`. Just run

```sh
pg_virtualenv ctest
```

`pg_virtualenv` creates a separate postgres server instance. The test databases
are created in this instance and the complete server is destroyed after the
tests are finished. ctest also calls appropriate fixtures that create the
separate tablespace required for some tests.

When running without `pg_virtualenv`, you need to ensure that PostgreSQL is
running and that your user is a superuser of that system. You also need to
create an appropriate test tablespace manually. To do that, run:

```sh
sudo -u postgres createuser -s $USER
sudo mkdir -p /tmp/psql-tablespace
sudo chown postgres.postgres /tmp/psql-tablespace
psql -c "CREATE TABLESPACE tablespacetest LOCATION '/tmp/psql-tablespace'" postgres
```

Once this is all set up, all the tests should run (no SKIPs), and pass (no
FAILs). If you find something which seems to be a bug, please check to see if
it is a known issue at https://github.com/openstreetmap/osm2pgsql/issues and,
if it's not already known, report it there.

If you have failing tests and want to look at the test database to figure out
what's happening, you can set the environment variable `OSM2PGSQL_KEEP_TEST_DB`
to anything. This will disable the database cleanup at the end of the test.
This will often be used together with the `-s` option of `pg_virtualenv` which
drops you into a shell after a failed test where you can still access the
database created by `pg_virtualenv`.

### Performance Testing

If performance testing with a full planet import is required, indicate what
needs testing in a pull request.

## Coverage reports

To create coverage reports, set `BUILD_COVERAGE` in the CMake config to `ON`,
compile and run the tests. Then run `make coverage`. This will generate a
coverage report in `coverage/index.html` in the build directory.

For this to work you need a coverage tool installed. For GCC this is `gcov`,
for Clang this is `llvm-cov` in the right version. CMake will automatically
try to find the correct tool. In any case the tool `gcovr` is used to create
the report.

## Releasing a new version

* Decide on a new version. (See [semantic versioning](https://semver.org/).)
* Update version in [CMakeLists.txt](CMakeLists.txt), look for `PACKAGE_VERSION`
* Build man page (`make man`) and copy it to `docs/osm2pgsql.1`.
* ...

## Maintainers

The current maintainers of osm2pgsql are [Sarah Hoffmann](https://github.com/lonvia/)
and [Paul Norman](https://github.com/pnorman/).
