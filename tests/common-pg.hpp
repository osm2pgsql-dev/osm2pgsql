#ifndef COMMON_PG_HPP
#define COMMON_PG_HPP

#include <string>
#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <libpq-fe.h>

/* Some RAII objects to make writing stuff that needs a temporary database
 * easier, and to keep track of and free connections and results objects.
 */
namespace pg {

struct conn;
struct result;

typedef boost::shared_ptr<conn> conn_ptr;
typedef boost::shared_ptr<result> result_ptr;

struct conn
    : public boost::noncopyable,
      public boost::enable_shared_from_this<conn> {

    static conn_ptr connect(const std::string &conninfo);
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

    const std::string &conninfo() const;

    void check_tblspc();

private:
    void setup_extension(conn_ptr db, const std::string &extension, ...);

    conn_ptr m_conn;
    std::string m_db_name;
    std::string m_conninfo;
};

} // namespace pg

#endif /* COMMON_PG_HPP */
