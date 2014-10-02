#include "common-pg.hpp"

#include <sstream>
#include <cstdarg>
#include <unistd.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/format.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/algorithm/string/predicate.hpp>

#ifdef _MSC_VER
#include <windows.h>
#include <process.h>
#define getpid _getpid
#define sleep Sleep
#endif

namespace fs = boost::filesystem;

namespace pg {

conn_ptr conn::connect(const std::string &conninfo) {
    return boost::shared_ptr<conn>(new conn(conninfo));
}

result_ptr conn::exec(const std::string &query) {
    return boost::make_shared<result>(shared_from_this(), query);
}

result_ptr conn::exec(const boost::format &fmt) {
    return exec(fmt.str());
}

PGconn *conn::get() { return m_conn; }

conn::~conn() {
    if (m_conn != NULL) {
        PQfinish(m_conn);
        m_conn = NULL;
    }
}

conn::conn(const std::string &conninfo)
    : m_conn(PQconnectdb(conninfo.c_str())) {
    if (PQstatus(m_conn) != CONNECTION_OK) {
        std::ostringstream out;
        out << "Could not connect to database \"" << conninfo
            << "\" because: " << PQerrorMessage(m_conn);
        PQfinish(m_conn);
        m_conn = NULL;
        throw std::runtime_error(out.str());
    }
}

result::result(conn_ptr conn, const std::string &query)
    : m_conn(conn), m_result(PQexec(conn->get(), query.c_str())) {
    if (m_result == NULL) {
        throw std::runtime_error((boost::format("Unable to run query \"%1%\": NULL result")
                                  % query).str());
    }
}

PGresult *result::get() { return m_result; }

result::~result() {
    if (m_result != NULL) {
        PQclear(m_result);
        m_result = NULL;
    }
}

tempdb::tempdb()
    : m_conn(conn::connect("dbname=postgres")) {
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
    
    m_db_name = (boost::format("osm2pgsql-test-%1%-%2%") % getpid() % time(NULL)).str();
    m_conn->exec(boost::format("DROP DATABASE IF EXISTS \"%1%\"") % m_db_name);
    //tests can be run concurrently which means that this query can collide with other similar ones
    //so we implement a simple retry here to get around the case that they do collide if we dont
    //we often fail due to both trying to access template1 at the same time
    size_t retries = 0;
    ExecStatusType status = PGRES_FATAL_ERROR;
    while(status != PGRES_COMMAND_OK && retries++ < 20)
    {
        sleep(1);
        res = m_conn->exec(boost::format("CREATE DATABASE \"%1%\" WITH ENCODING 'UTF8'") % m_db_name);
        status = PQresultStatus(res->get());
    }
    if (PQresultStatus(res->get()) != PGRES_COMMAND_OK) {
        throw std::runtime_error((boost::format("Could not create a database: %1%") 
                                  % PQresultErrorMessage(res->get())).str());
    }
    
    m_conninfo = (boost::format("dbname=%1%") % m_db_name).str();
    conn_ptr db = conn::connect(m_conninfo);
    
    setup_extension(db, "postgis", "postgis-1.5/postgis.sql", "postgis-1.5/spatial_ref_sys.sql", NULL);
    setup_extension(db, "hstore", NULL);
}

tempdb::~tempdb() {
    if (m_conn) {
        m_conn->exec(boost::format("DROP DATABASE IF EXISTS \"%1%\"") % m_db_name);
    }
}

const std::string &tempdb::conninfo() const {
    return m_conninfo;
}

void tempdb::setup_extension(conn_ptr db, const std::string &extension, ...) {
    // first, try the new way of setting up extensions
    result_ptr res = db->exec(boost::format("CREATE EXTENSION %1%") % extension);
    if (PQresultStatus(res->get()) != PGRES_COMMAND_OK) {
        // if that fails, then fall back to trying to find the files on
        // the filesystem to load to create the extension.
        res = db->exec("select regexp_replace(split_part(version(),' ',2),'\\.[0-9]*$','');");
        
        if ((PQresultStatus(res->get()) != PGRES_TUPLES_OK) ||
            (PQntuples(res->get()) != 1)) {
            throw std::runtime_error("Unable to determine PostgreSQL version.");
        }
        std::string pg_version(PQgetvalue(res->get(), 0, 0));
        
        // Guess the directory from the postgres version.
        // TODO: make the contribdir configurable. Probably
        // only works on Debian-based distributions at the moment.
        fs::path contribdir = fs::path("/usr/share/postgresql/") / pg_version / fs::path("contrib");
        va_list ap;
        va_start(ap, extension);
        const char *str = NULL;
        while ((str = va_arg(ap, const char *)) != NULL) {
            fs::path sql_file = contribdir / fs::path(str);
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
                res = db->exec(sql);
                if (PQresultStatus(res->get()) != PGRES_COMMAND_OK) {
                    throw std::runtime_error((boost::format("Could not load extension \"%1%\": %2%") 
                                              % extension
                                              % PQresultErrorMessage(res->get())).str());
                }
            }
        }
        va_end(ap);
    }
}
} // namespace pg
