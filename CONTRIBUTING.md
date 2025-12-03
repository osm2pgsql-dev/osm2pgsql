# osm2pgsql contribution guidelines

The following section describes our work flow, coding style and give some
hints on developer setup.
For more information about what to contribute and an overview
of the general roadmap, visit the [Contribution guide](https://osm2pgsql.org/contribute/)
on the osm2pgsql website.

## Workflow

We operate with the
["Fork & Pull"](https://help.github.com/articles/using-pull-requests) model
and try to stick to a four-eyes review mode, meaning that PRs should be merged
by a different person than the author.

Here are a few simple rules you should follow with your code and pull request (PR).
They will maximize your chances that a PR gets reviewed and merged.

* Split your PR into functionally sensible commits. Describe each commit with
  a relevant commit message. If you need to do fix-up commits, please, merge
  them into the functional commits. Interactive rebasing (`git rebase -i`) is
  very useful for this. Then force-push to your PR branch.
* Avoid merge commits. If you have to catch up with changes from master,
  rather use rebasing.
* Split up larger PRs into smaller units if possible. Never mix two different
  topics or fixes in a single PR.
* Decorate your PR with an informative but succinct description. Do not post
  AI-generated PR descriptions without having reviewed (and preferably heavily
  shortened) the text.
* Try to follow the style of existing code as close as possible. Use
  clang-format to follow the formal coding style (see below).

> [!IMPORTANT]
> Any use of generative AI for writing code, documentation or PR descriptions
> must be disclosed. You must further be able to show that you have understood
> the generated parts. Your code, your responsibility.

## Coding style

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

The manpages are rebuilt and checked into the repository as part of the
release process.

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

`pg_virtualenv` creates a separate PostgreSQL server instance. The test databases
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
BDD tests you need to have behave and psycopg installed. On Ubuntu run:

```sh
sudo apt-get install python3-psycopg python3-behave
```

The BDD tests are run through the osm2pgsql-test-style tester. See the
[section on testing](https://osm2pgsql.org/doc/manual.html#style-testing)
in the manual for details.

There are ctest directives to run the tests. If you want to run the tests
manually, for example to run single tests during development, you can
use the `run-bdd-tests` script in the build directory. It is a thin
wrapper around osm2pgsql-test-style which properly sets up the paths
for the osm2pgsql binary and test data paths:

```sh
cd build
./run-bdd-tests ../tests/bdd/regression/
```

To run a single test, simply add the name of the test file, followed by a
column and the line number of the test:

```sh
./run-bdd-tests ../tests/bdd/flex/area.feature:71
```

You can pass any additional parameters to the script that osm2pgsql-test-style
would take. If you need to inspect the database that a test produces, you add
`--keep-test-db` and behave won't remove the database after the test is
finished. This makes only sense, when running a single test.
When running under pg_virtualenv, don't forget to keep the virtual environment
as well. You can use the handy `-s` switch:

```sh
pg_virtualenv -s ./run-bdd-tests ../tests/bdd/flex/area.feature:71
```

It drops you into a shell when the behave test fails, where you can use
psql to look at the database. Or start a shell in the virtual environment
with `pg_virtualenv bash` and run behave from there.

The BDDs automatically detect if osm2pgsql was compiled with
proj support and skip tests accordingly. They also check for the test
tablespace `tablespacetest` for tests that need tablespaces. To force
running the proj and tablespace tests use `--test-proj yes` and
`--test-tablespace yes` respectively.

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
