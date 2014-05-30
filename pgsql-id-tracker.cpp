#include "pgsql-id-tracker.hpp"

#include <libpq-fe.h>
#include <string>
#include <boost/format.hpp>

#include "osmtypes.hpp"
#include "pgsql.hpp"
#include "util.hpp"

struct pgsql_id_tracker::pimpl {
    pimpl(const std::string &conninfo, 
          const std::string &prefix, 
          const std::string &type,
          bool owns_table);
    ~pimpl();

    PGconn *conn;
    std::string table_name;
    bool owns_table;
    osmid_t old_id;
};

pgsql_id_tracker::pimpl::pimpl(const std::string &conninfo, 
                               const std::string &prefix, 
                               const std::string &type,
                               bool owns_table_) 
    : conn(PQconnectdb(conninfo.c_str())),
      table_name((boost::format("%1%_%2%") % prefix % type).str()),
      owns_table(owns_table_),
      old_id(0) {
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        util::exit_nicely();
    }
    if (owns_table) {
        pgsql_exec(conn, PGRES_COMMAND_OK,
                   "DROP TABLE IF EXISTS \"%s\"",
                   table_name.c_str());
        pgsql_exec(conn, PGRES_COMMAND_OK,
                   "CREATE TABLE \"%s\" (id " POSTGRES_OSMID_TYPE ")",
                   table_name.c_str());
    }
    pgsql_exec(conn, PGRES_COMMAND_OK,
               "PREPARE set_mark(" POSTGRES_OSMID_TYPE ") AS INSERT INTO \"%s\" (id) "
               "SELECT $1 WHERE NOT EXISTS (SELECT id FROM \"%s\" WHERE id = $1)",
               table_name.c_str(), table_name.c_str());
    pgsql_exec(conn, PGRES_COMMAND_OK,
               "PREPARE get_mark(" POSTGRES_OSMID_TYPE ") AS SELECT id FROM \"%s\" "
               "WHERE id = $1",
               table_name.c_str());
    pgsql_exec(conn, PGRES_COMMAND_OK,
               "PREPARE get_min AS SELECT min(id) AS id FROM \"%s\"",
               table_name.c_str());
    pgsql_exec(conn, PGRES_COMMAND_OK,
               "PREPARE drop_mark(" POSTGRES_OSMID_TYPE ") AS DELETE FROM \"%s\" "
               "WHERE id = $1",
               table_name.c_str());
    pgsql_exec(conn, PGRES_COMMAND_OK, "BEGIN");
}

pgsql_id_tracker::pimpl::~pimpl() {
    if (conn) {
        pgsql_exec(conn, PGRES_COMMAND_OK, "COMMIT");
        if (owns_table) {
            pgsql_exec(conn, PGRES_COMMAND_OK, "DROP TABLE \"%s\"", table_name.c_str());
        }
        PQfinish(conn);
    }
    conn = NULL;
}

pgsql_id_tracker::pgsql_id_tracker(const std::string &conninfo, 
                                   const std::string &prefix, 
                                   const std::string &type,
                                   bool owns_table) 
    : impl() {
    impl.reset(new pimpl(conninfo, prefix, type, owns_table));
}

pgsql_id_tracker::~pgsql_id_tracker() {
}

void pgsql_id_tracker::mark(osmid_t id) {
    char tmp[16];
    char const *paramValues[1];

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;
    
    pgsql_execPrepared(impl->conn, "set_mark", 1, paramValues, PGRES_COMMAND_OK);
}

bool pgsql_id_tracker::is_marked(osmid_t id) {
    char tmp[16];
    char const *paramValues[1] = {NULL};
    PGresult *result = NULL;

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;
    
    result = pgsql_execPrepared(impl->conn, "get_mark", 1, paramValues, PGRES_TUPLES_OK);
    bool done = PQntuples(result) > 0;
    PQclear(result);
    return done;
}

osmid_t pgsql_id_tracker::pop_mark() {
    osmid_t id = std::numeric_limits<osmid_t>::max();
    PGresult *result = NULL;

    result = pgsql_execPrepared(impl->conn, "get_min", 0, NULL, PGRES_TUPLES_OK);
    if ((PQntuples(result) == 1) &&
        (PQgetisnull(result, 0, 0) == 0)) {
        id = strtoosmid(PQgetvalue(result, 0, 0), NULL, 10);
    }

    PQclear(result);

    if (id != std::numeric_limits<osmid_t>::max()) {
        unmark(id);
    }

    assert((id > impl->old_id) || (id == std::numeric_limits<osmid_t>::max()));
    impl->old_id = id;

    return id;
}

void pgsql_id_tracker::unmark(osmid_t id) {
    char tmp[16];
    char const *paramValues[1] = {NULL};

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;
    
    pgsql_execPrepared(impl->conn, "drop_mark", 1, paramValues, PGRES_COMMAND_OK);
}

void pgsql_id_tracker::commit() {
    if (impl->owns_table) {
        pgsql_exec(impl->conn, PGRES_COMMAND_OK, "CREATE INDEX ON \"%s\" (id)", impl->table_name.c_str());
    }
    pgsql_exec(impl->conn, PGRES_COMMAND_OK, "COMMIT");
    pgsql_exec(impl->conn, PGRES_COMMAND_OK, "BEGIN");
}

void pgsql_id_tracker::force_release() {
    impl->owns_table = false;
    impl->conn = NULL;
}
