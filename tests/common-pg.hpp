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

    static std::unique_ptr<pg::tempdb> create_db_or_skip();

    database_options_t database_options;

    void check_tblspc();
    /**
     * Checks the result of a query with COUNT(*).
     * It will work with any integer-returning query.
     * \param[in] expected expected result
     * \param[in] query SQL query to run. Must return one tuple.
     * \throws std::runtime_error if query result is not expected
     */
    void check_count(int expected, const std::string &query);

    /**
     * Checks a floating point number.
     * It allows a small variance around the expected result to allow for
     * floating point differences.
     * The query must only return one tuple
     * \param[in] expected expected result
     * \param[in] query SQL query to run. Must return one tuple.
     * \throws std::runtime_error if query result is not expected
     */
    void check_number(double expected, const std::string &query);

    /**
     * Check the string a query returns.
     * \param[in] expected expected result
     * \param[in] query SQL query to run. Must return one tuple.
     * \throws std::runtime_error if query result is not expected
     */
    void check_string(const std::string &expected, const std::string &query);
    /**
     * Assert that the database has a certain table_name
     * \param[in] table_name Name of the table to check, optionally schema-qualified
     * \throws std::runtime_error if missing the table
     */
    void assert_has_table(const std::string &table_name);

private:
    /**
     * Sets up an extension, trying first with 9.1 CREATE EXTENSION, and falling
     * back to trying to find extension_files. The fallback is not likely to
     * work on systems not based on Debian.
     */
    void setup_extension(const std::string &extension, const std::vector<std::string> &extension_files = std::vector<std::string>());

    conn_ptr m_conn; ///< connection to the test DB
    conn_ptr m_postgres_conn; ///< Connection to the "postgres" db, used to create and drop test DBs
};

} // namespace pg

#endif /* COMMON_PG_HPP */
