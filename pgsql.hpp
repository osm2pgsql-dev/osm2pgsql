/* Helper functions for pgsql access */

/* Current middle and output-pgsql do a lot of things similarly, this should
 * be used to abstract to commonalities */

#ifndef PGSQL_H
#define PGSQL_H

#include <string>
#include <cstring>
#include <libpq-fe.h>
#include <memory>

struct pg_result_deleter_t
{
    void operator()(PGresult *p) const { PQclear(p); }
};

typedef std::unique_ptr<PGresult, pg_result_deleter_t> pg_result_t;

pg_result_t pgsql_execPrepared(PGconn *sql_conn, const char *stmtName,
                               int nParams, const char *const *paramValues,
                               ExecStatusType expect);
void pgsql_CopyData(const char *context, PGconn *sql_conn, std::string const &sql);

pg_result_t pgsql_exec_simple(PGconn *sql_conn, ExecStatusType expect,
                              std::string const &sql);
pg_result_t pgsql_exec_simple(PGconn *sql_conn, ExecStatusType expect,
                              const char *sql);

int pgsql_exec(PGconn *sql_conn, const ExecStatusType expect, const char *fmt, ...)
#ifndef _MSC_VER
 __attribute__ ((format (printf, 3, 4)))
#endif
;

#endif
