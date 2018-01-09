#include "common-pg.hpp"

#include <iostream>
#include <sstream>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <memory>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>

#ifdef _MSC_VER
#include <windows.h>
#include <process.h>
#define getpid _getpid
#define sleep Sleep
#endif

namespace fs = boost::filesystem;

namespace pg {

conn_ptr conn::connect(const std::string &conninfo) {
    return std::shared_ptr<conn>(new conn(conninfo));
}

conn_ptr conn::connect(const database_options_t &database_options) {
    return std::shared_ptr<conn>(new conn(database_options.conninfo()));
}

result_ptr conn::exec(const std::string &query) {
    return std::make_shared<result>(shared_from_this(), query);
}

result_ptr conn::exec(const boost::format &fmt) {
    return exec(fmt.str());
}

PGconn *conn::get() { return m_conn; }

conn::~conn() {
    if (m_conn != nullptr) {
        PQfinish(m_conn);
        m_conn = nullptr;
    }
}

conn::conn(const std::string &conninfo)
    : m_conn(PQconnectdb(conninfo.c_str())) {
    if (PQstatus(m_conn) != CONNECTION_OK) {
        std::ostringstream out;
        out << "Could not connect to database \"" << conninfo
            << "\" because: " << PQerrorMessage(m_conn);
        PQfinish(m_conn);
        m_conn = nullptr;
        throw std::runtime_error(out.str());
    }
}

result::result(conn_ptr conn, const std::string &query)
    : m_conn(conn), m_result(PQexec(conn->get(), query.c_str())) {
    if (m_result == nullptr) {
        throw std::runtime_error((boost::format("Unable to run query \"%1%\": NULL result")
                                  % query).str());
    }
}

PGresult *result::get() { return m_result; }

result::~result() {
    if (m_result != nullptr) {
        PQclear(m_result);
        m_result = nullptr;
    }
}

tempdb::tempdb()
    : m_postgres_conn(conn::connect("dbname=postgres"))
{
    result_ptr res = nullptr;
    // osm2pgsql doesn't require a db name, but subsequent uses of database_options.db.get() in this file assume one is set
    database_options.db = (boost::format("osm2pgsql-test-%1%-%2%") % getpid() % time(nullptr)).str();
    m_postgres_conn->exec(boost::format("DROP DATABASE IF EXISTS \"%1%\"") % database_options.db.get());
    //tests can be run concurrently which means that this query can collide with other similar ones
    //so we implement a simple retry here to get around the case that they do collide if we dont
    //we often fail due to both trying to access template1 at the same time
    size_t retries = 0;
    ExecStatusType status = PGRES_FATAL_ERROR;
    while(status != PGRES_COMMAND_OK && retries++ < 20)
    {
        sleep(1);
        res = m_postgres_conn->exec(boost::format("CREATE DATABASE \"%1%\" WITH ENCODING 'UTF8'") % database_options.db.get());
        status = PQresultStatus(res->get());
    }
    if (PQresultStatus(res->get()) != PGRES_COMMAND_OK) {
        throw std::runtime_error((boost::format("Could not create a database: %1%")
                                  % PQresultErrorMessage(res->get())).str());
    }

    m_conn = conn::connect(database_options);

    setup_extension("postgis", {"postgis-1.5/postgis.sql", "postgis-1.5/spatial_ref_sys.sql"});
    setup_extension("hstore");
}

std::unique_ptr<pg::tempdb> tempdb::create_db_or_skip()
{
    std::unique_ptr<pg::tempdb> db;

    try {
        db.reset(new pg::tempdb);
    } catch (const std::exception &e) {
        std::cerr << "Unable to setup database: " << e.what() << "\n";
        exit(77); // <-- code to skip this test.
    }

    return db;
}

void tempdb::check_tblspc() {
  result_ptr res = m_conn->exec("SELECT spcname FROM pg_tablespace WHERE "
                                "spcname = 'tablespacetest'");
  if ((PQresultStatus(res->get()) != PGRES_TUPLES_OK) ||
      (PQntuples(res->get()) != 1)) {
      std::ostringstream out;
      out << "The test needs a temporary tablespace to run in, but it does not "
          << "exist. Please create the temporary tablespace. On Linux, you can "
          << "do this by running:\n"
          << "  sudo mkdir -p /tmp/psql-tablespace\n"
          << "  sudo /bin/chown postgres.postgres /tmp/psql-tablespace\n"
          << "  psql -c \"CREATE TABLESPACE tablespacetest LOCATION "
          << "'/tmp/psql-tablespace'\" postgres\n";
      throw std::runtime_error(out.str());
  }

}

void tempdb::check_count(int expected, const std::string &query) {
    pg::result_ptr res = m_conn->exec(query);
    if (PQresultStatus(res->get()) != PGRES_TUPLES_OK) {
        throw std::runtime_error((boost::format("Expected PGRES_TUPLES_OK but "
                                                "got %1%. %2% Query was %3%.")
                                  % PQresStatus(PQresultStatus(res->get()))
                                  % PQresultErrorMessage(res->get())
                                  % query).str());
    }
    int ntuples = PQntuples(res->get());
    if (ntuples != 1) {
        throw std::runtime_error((boost::format("Expected one tuple from a query to check "
                                                "COUNT(*), but got %1%. Query was: %2%.")
                                  % ntuples % query).str());
    }

    std::string numstr = PQgetvalue(res->get(), 0, 0);
    int count = boost::lexical_cast<int>(numstr);

    if (count != expected) {
        throw std::runtime_error((boost::format("Expected %1%, but got %2%, when running "
                                                "query: %3%.")
                                  % expected % numstr % query).str());
    }
}

void tempdb::check_number(double expected, const std::string &query) {
    pg::result_ptr res = m_conn->exec(query);

    int ntuples = PQntuples(res->get());
    if (ntuples != 1) {
        throw std::runtime_error((boost::format("Expected only one tuple from a query, "
                                                " but got %1%. Query was: %2%.")
                                  % ntuples % query).str());
    }

    std::string numstr = PQgetvalue(res->get(), 0, 0);
    double num = boost::lexical_cast<double>(numstr);

    // floating point isn't exact, so allow a 0.01% difference
    if ((num > 1.0001*expected) || (num < 0.9999*expected)) {
        throw std::runtime_error((boost::format("Expected %1%, but got %2%, when running "
                                                "query: %3%.")
                                  % expected % num % query).str());
    }
}

void tempdb::check_string(const std::string &expected, const std::string &query) {
    pg::result_ptr res = m_conn->exec(query);

    int ntuples = PQntuples(res->get());
    if (ntuples != 1) {
        throw std::runtime_error((boost::format("Expected only one tuple from a query, "
                                                " but got %1%. Query was: %2%.")
                                  % ntuples % query).str());
    }

    std::string actual = PQgetvalue(res->get(), 0, 0);

    if (actual != expected) {
        throw std::runtime_error((boost::format("Expected %1%, but got %2%, when running "
                                                "query: %3%.")
                                  % expected % actual % query).str());
    }
}

void tempdb::assert_has_table(const std::string &table_name) {
    std::string query = (boost::format("select count(*) from pg_catalog.pg_class "
                                     "where oid = '%1%'::regclass")
                       % table_name).str();

    check_count(1, query);
}

tempdb::~tempdb() {
    m_conn.reset(); // close the connection to the db
    if (m_postgres_conn) {
        m_postgres_conn->exec(boost::format("DROP DATABASE IF EXISTS \"%1%\"") % database_options.db.get());
    }
}

void tempdb::setup_extension(const std::string &extension,
                             const std::vector<std::string> &extension_files) {
    // first, try the new way of setting up extensions
    result_ptr res = m_conn->exec(boost::format("CREATE EXTENSION %1%") % extension);
    if (PQresultStatus(res->get()) != PGRES_COMMAND_OK) {
        if (extension_files.size() == 0) {
            throw std::runtime_error((boost::format("Unable to load extension %1% and no files specified") % extension).str());
        }
        // if that fails, then fall back to trying to find the files on
        // the filesystem to load to create the extension.
        res = m_conn->exec("select regexp_replace(split_part(version(),' ',2),'\\.[0-9]*$','');");

        if ((PQresultStatus(res->get()) != PGRES_TUPLES_OK) ||
            (PQntuples(res->get()) != 1)) {
            throw std::runtime_error("Unable to determine PostgreSQL version.");
        }
        std::string pg_version(PQgetvalue(res->get(), 0, 0));

        // Guess the directory from the postgres version.
        // TODO: make the contribdir configurable. Probably
        // only works on Debian-based distributions at the moment.
        fs::path contribdir = fs::path("/usr/share/postgresql/") / pg_version / fs::path("contrib");

        for (const auto& filename : extension_files) {
            fs::path sql_file = contribdir / fs::path(filename);
            // Should this throw an error if the file can't be found?
            if (fs::exists(sql_file) && fs::is_regular_file(sql_file)) {
                size_t size = fs::file_size(sql_file);
                std::string sql(size + 1, '\0');
                fs::ifstream in(sql_file);
                in.read(&sql[0], size);
                if (in.fail() || in.bad()) {
                    throw std::runtime_error((boost::format("Unable to read file %1% while trying to "
                                                            "load extension \"%2%\".")
                                              % sql_file % extension).str());
                }
                res = m_conn->exec(sql);
                if (PQresultStatus(res->get()) != PGRES_COMMAND_OK) {
                    throw std::runtime_error((boost::format("Could not load extension \"%1%\": %2%")
                                              % extension
                                              % PQresultErrorMessage(res->get())).str());
                }
            }
        }
    }
}
} // namespace pg
