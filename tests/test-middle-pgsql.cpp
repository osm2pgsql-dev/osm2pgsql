#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <cstdarg>

#include "osmtypes.hpp"
#include "middle.hpp"
#include "output.hpp"
#include "output-null.hpp"
#include "middle-pgsql.hpp"

#include <libpq-fe.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/format.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "tests/middle-tests.hpp"

namespace fs = boost::filesystem;

void exit_nicely()
{
    fprintf(stderr, "Error occurred, cleaning up\n");
    throw std::runtime_error("Error occurred, cleaning up.");
}

namespace pg {
struct conn;
struct result;

typedef boost::shared_ptr<conn> conn_ptr;
typedef boost::shared_ptr<result> result_ptr;

struct conn
  : public boost::noncopyable,
    public boost::enable_shared_from_this<conn> {

  static conn_ptr connect(const std::string &conninfo) {
    return boost::shared_ptr<conn>(new conn(conninfo));
  }

  result_ptr exec(const std::string &query) {
    return boost::make_shared<result>(shared_from_this(), query);
  }

  result_ptr exec(const boost::format &fmt) {
    return exec(fmt.str());
  }

  PGconn *get() { return m_conn; }

  ~conn() {
    if (m_conn != NULL) {
      PQfinish(m_conn);
      m_conn = NULL;
    }
  }

private:
  conn(const std::string &conninfo)
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

  PGconn *m_conn;
};

struct result
  : public boost::noncopyable {
  result(conn_ptr conn, const std::string &query)
    : m_conn(conn), m_result(PQexec(conn->get(), query.c_str())) {
    if (m_result == NULL) {
      throw std::runtime_error((boost::format("Unable to run query \"%1%\": NULL result")
                                % query).str());
    }
  }

  PGresult *get() { return m_result; }

  ~result() {
    if (m_result != NULL) {
      PQclear(m_result);
      m_result = NULL;
    }
  }

private:
  conn_ptr m_conn;
  PGresult *m_result;
};

struct tempdb 
  : public boost::noncopyable {

  tempdb()
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
    res = m_conn->exec(boost::format("CREATE DATABASE \"%1%\" WITH ENCODING 'UTF8'") % m_db_name);
    if (PQresultStatus(res->get()) != PGRES_COMMAND_OK) {
      throw std::runtime_error((boost::format("Could not create a database: %1%") 
                                % PQresultErrorMessage(res->get())).str());
    }

    m_conninfo = (boost::format("dbname=%1%") % m_db_name).str();
    conn_ptr db = conn::connect(m_conninfo);

    setup_extension(db, "postgis", "postgis-1.5/postgis.sql", "postgis-1.5/spatial_ref_sys.sql", NULL);
    setup_extension(db, "hstore", NULL);
  }

  ~tempdb() {
    if (m_conn) {
      m_conn->exec(boost::format("DROP DATABASE IF EXISTS \"%1%\"") % m_db_name);
    }
  }

  const std::string &conninfo() const {
    return m_conninfo;
  }

private:
  void setup_extension(conn_ptr db, const std::string &extension, ...) {
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

  conn_ptr m_conn;
  std::string m_db_name;
  std::string m_conninfo;
};
} // namespace pg

int main(int argc, char *argv[]) {
  boost::scoped_ptr<pg::tempdb> db;

  try {
    db.reset(new pg::tempdb);
  } catch (const std::exception &e) {
    std::cerr << "Unable to setup database: " << e.what() << "\n";
    return 77; // <-- code to skip this test.
  }

  struct middle_pgsql_t mid_pgsql;
  struct output_options options; memset(&options, 0, sizeof options);
  options.conninfo = db->conninfo().c_str();
  options.scale = 10000000;
  options.num_procs = 1;
  options.prefix = "osm2pgsql_test";
  options.tblsslim_index = "tablespacetest";
  options.tblsslim_data = "tablespacetest";
  options.slim = 1;

  struct output_null_t out_test(&mid_pgsql, &options);

  try {
    // start an empty table to make the middle create the
    // tables it needs. we then run the test in "append" mode.
    mid_pgsql.start(&options);
    mid_pgsql.commit();
    mid_pgsql.stop();

    options.append = 1; /* <- needed because we're going to change the
                         *    data and check that the updates fire. */

    mid_pgsql.start(&options);
    
    int status = 0;
    
    status = test_node_set(&mid_pgsql);
    if (status != 0) { mid_pgsql.stop(); throw std::runtime_error("test_node_set failed."); }
    
    status = test_way_set(&mid_pgsql);
    if (status != 0) { mid_pgsql.stop(); throw std::runtime_error("test_way_set failed."); }
    
    mid_pgsql.commit();
    mid_pgsql.stop();
    
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;

  } catch (...) {
    std::cerr << "UNKNOWN ERROR" << std::endl;
  }

  return 1;
}
