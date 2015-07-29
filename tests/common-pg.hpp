#ifndef COMMON_PG_HPP
#define COMMON_PG_HPP

#include <string>
#include <memory>
#include <boost/format.hpp>
#include <boost/noncopyable.hpp>
#include <libpq-fe.h>

// reuse the database_options_t class
#include "options.hpp"

/* Some RAII objects to make writing stuff that needs a temporary database
 * easier, and to keep track of and free connections and results objects.
 */
namespace pg {

struct conn;
struct result;

typedef std::shared_ptr<conn> conn_ptr;
typedef std::shared_ptr<result> result_ptr;

struct conn
    : public boost::noncopyable,
      public std::enable_shared_from_this<conn> {

    static conn_ptr connect(const std::string &conninfo);
    static conn_ptr connect(const database_options_t &database_options);
    result_ptr exec(const std::string &query);
    result_ptr exec(const boost::format &fmt);
    PGconn *get();

    ~conn();

private:
    conn(const std::string &conninfo);

    PGconn *m_conn;
};

struct result
  : public boost::noncopyable {
    result(conn_ptr conn, const std::string &query);
    PGresult *get();
    ~result();

private:
    conn_ptr m_conn;
    PGresult *m_result;
};

struct tempdb
  : public boost::noncopyable {

    tempdb();
    ~tempdb();

    database_options_t database_options;

    void check_tblspc();

private:
    // Sets up an extension, trying first with 9.1 CREATE EXTENSION, and falling back to trying to find extension_files
    void setup_extension(conn_ptr db, const std::string &extension, const std::vector<std::string> &extension_files = std::vector<std::string>());

    conn_ptr m_conn;
};

} // namespace pg

#endif /* COMMON_PG_HPP */
