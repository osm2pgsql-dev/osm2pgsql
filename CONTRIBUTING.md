# Osm2pgsql contribution guidelines

## Workflow

We operate the "Fork & Pull" model explained at

https://help.github.com/articles/using-pull-requests

You should fork the project into your own repo, create a topic branch there and
then make one or more pull requests back to the OpenStreetMap repository. Your
pull requests will then be reviewed and discussed.

## History

To understand the osm2pgsql code, it helps to know some history on it.
Osm2pgsql was written in C in 2007 as a port of an older Python utility. In
2014 it was ported to C++ by MapQuest and the last C version was released as
0.86.0. In its time, it has had varying contribution activity, including times
with no maintainer or active developers.

Very few parts of the code now show their C origin, most has been transformed
to modern C++. We are currently targeting C++17.

## Versioning

Osm2pgsql uses [semantic versioning](https://semver.org/).

Bugs and known issues are fixed on the main branch only. Exceptions may be made
for severe bugs.

## Code style

Code must be written in the
[K&R 1TBS style](https://en.wikipedia.org/wiki/Indent_style#Variant:_1TBS) with
4 spaces indentation. Tabs should never be used in the C++ code. Braces must
always be used for code blocks, even one-liners.

Names should use underscores, not camel case, with class/struct names ending in
`_t`. Constants and template parameters must use all upper case.

Header files should be included in the following order, each group in their own
block:

* The corresponding .hpp file (in .cpp files only)
* Other osm2pgsql header files
* Header files from external libraries, each in their own block
* C++/C C++ standard library header files

There is a .clang-format configuration available and all code must be run
through clang-format before submitting. You can use git-clang-format after
staging all your changes:

    git-clang-format src/*pp tests/*pp

clang-format 7 or later is required.

Comments in code should follow the [Doxygen
convention](https://www.doxygen.nl/manual/docblocks.html) using backslashes
(not @-signs) for commands.

## Documentation

User documentation is available on [the website](https://osm2pgsql.org/). The
source of the web page is in its own repository
(https://github.com/openstreetmap/osm2pgsql-website).

The man pages for [osm2pgsql](man/osm2pgsql.1) and
[osm2pgsql-replication](man/osm2pgsql-replication.1) can be built from source
with `make man`.

They need pandoc and argparse-manpage for the conversion. These tools can be
installed with:

```sh
sudo apt-get install pandoc python3-argparse-manpage
```

Results should be checked into the repository.

## Platforms targeted

Osm2pgsql must compile and pass all tests at least on Linux, OS X and Windows.
Tests run on Github action to make sure that it does.

On Linux the latest stable versions of popular distributions and the stable
version before that are supported if possible.

All maintained versions of PostgreSQL are supported.

## Testing

osm2pgsql is tested with two types of tests: Classic tests written in C++ and
BDD (Behavior Driven Development) tests written in Python.

### Classic Tests

The code comes with a suite of tests. They are only compiled and run when
`BUILD_TESTS=ON` is set in the CMake config.

Tests are executed by calling `ctest`. You can call `ctest` with `-L NoDB` to
only run tests that don't need a database.

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

### BDD Tests

Tests in the `tests/bdd` directory use [behave](https://github.com/behave/behave),
a Python implementation of a behaviour-driven test framework. To run the
BDD tests you need to have behave and psycopg2 installed. On Ubuntu run:

```sh
sudo apt-get install python3-psycopg2 python3-behave
```

There are ctest directives to run the tests. If you want to run the tests
manually, for example to run single tests during development, you can
switch to the bdd test directory and run behave directly from there:

```sh
cd osm2pgsql/tests/bdd
behave -DBINARY=<your build directory>/osm2pgsql
```

Per default, behave assumes that the build directory is under `osm2pgsql/build`.
If your setup works like that, you can leave out the `-D` parameter.

To make this a bit easier a shell script `run-behave` is provided in your
build directory which sets those correct paths and calls `behave`. If run
with `-p` as first option it will wrap the call to `behave` in a call to
`pg_virtualenv` for your convenience. All other command line parameters of
`run-behave` will be passed through to behave.

To run a single test, simply add the name of the test file, followed by a
column and the line number of the test:

```sh
behave flex/area.feature:71
```

If you need to inspect the database that a test produces, you can add
`-DKEEP_TEST_DB` and behave won't remove the database after the test is
finished. This makes of course only sense, when running a single test.
When running under pg_virtualenv, don't forget to keep the virtual environment
as well. You can use the handy `-s` switch:

```sh
pg_virtualenv -s behave -DKEEP_TEST_DB flex/area.feature:71
```

It drops you into a shell when the behave test fails, where you can use
psql to look at the database. Or start a shell in the virtual environment
with `pg_virtualenv bash` and run behave from there.

The BDDs automatically detect if osm2pgsql was compiled with Lua and
proj support and skip tests accordingly. They also check for the test
tablespace `tablespacetest` for tests that need tablespaces.

BDD tests hide print statements by default. For development purposes they
can be shown by adding these lines to `tests/bdd/.behaverc`:

```
color=False
stdout_capture=False
stderr_capture=False
log_capture=False
```

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
* Update version in [CMakeLists.txt](CMakeLists.txt), look for `project` function.
* Update man pages
  * Build man page: `make man`
  * Copy to source: `cp man/*1 ../man/`
* Tag release with release notes in commit message and upload the tag to Github.
* Fill out release notes on Github.
* Copy Windows binaries and source tarball to osm2pgsql.org.
* Add release info to osm2pgsql.org.
* Publish release notes as News article on osm2pgsql.org.

## Maintainers

The current maintainers of osm2pgsql are [Sarah Hoffmann](https://github.com/lonvia/)
and [Paul Norman](https://github.com/pnorman/).
