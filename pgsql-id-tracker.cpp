#include "pgsql-id-tracker.hpp"

#include <libpq-fe.h>
#include <string>
#include <boost/format.hpp>

#include "osmtypes.hpp"
#include "pgsql.hpp"

struct pgsql_id_tracker::pimpl {
    pimpl(const char *conninfo, 
          const char *prefix, 
          const char *type,
          bool owns_table);
    ~pimpl();

    PGconn *conn;
    std::string table_name;
    bool owns_table;
};

pgsql_id_tracker::pimpl::pimpl(const char *conninfo, 
                               const char *prefix, 
                               const char *type,
                               bool owns_table_) 
    : conn(PQconnectdb(conninfo)),
      table_name((boost::format("%1%_%2%_done") % prefix % type).str()),
      owns_table(owns_table) {
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        exit_nicely();
    }
    if (owns_table) {
        pgsql_exec(conn, PGRES_COMMAND_OK,
                   "CREATE TEMPORARY TABLE \"%s\" (id " POSTGRES_OSMID_TYPE " PRIMARY KEY)",
                   table_name.c_str());
    }
    pgsql_exec(conn, PGRES_COMMAND_OK,
               "PREPARE mark_done(" POSTGRES_OSMID_TYPE ") AS INSERT INTO \"%s\" (id) "
               "SELECT $1 WHERE NOT EXISTS (SELECT id FROM \"%s\" WHERE id = $1)",
               table_name.c_str(), table_name.c_str());
    pgsql_exec(conn, PGRES_COMMAND_OK,
               "PREPARE get_done(" POSTGRES_OSMID_TYPE ") AS SELECT id FROM \"%s\" "
               "WHERE id = $1",
               table_name.c_str());
}

pgsql_id_tracker::pimpl::~pimpl() {
    if (owns_table) {
        pgsql_exec(conn, PGRES_COMMAND_OK, "DROP TABLE \"%s\"", table_name.c_str());
    }
    PQfinish(conn);
    conn = NULL;
}

pgsql_id_tracker::pgsql_id_tracker(const char *conninfo, 
                                   const char *prefix, 
                                   const char *type,
                                   bool owns_table) 
    : impl(new pimpl(conninfo, prefix, type, owns_table)) {
}

pgsql_id_tracker::~pgsql_id_tracker() {
}

void pgsql_id_tracker::done(osmid_t id) {
    char tmp[16];
    char const *paramValues[1];

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;
    
    pgsql_execPrepared(impl->conn, "mark_done", 1, paramValues, PGRES_COMMAND_OK);
}

bool pgsql_id_tracker::is_done(osmid_t id) {
    char tmp[16];
    char const *paramValues[1] = {NULL};
    PGresult *result = NULL;

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;
    
    result = pgsql_execPrepared(impl->conn, "get_done", 1, paramValues, PGRES_TUPLES_OK);
    bool done = PQntuples(result) == 1;
    PQclear(result);
    return done;
}
