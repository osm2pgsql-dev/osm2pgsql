/* Helper functions for pgsql access */

/* Current middle and output-pgsql do a lot of things similarly, this should
 * be used to abstract to commonalities */

#ifndef PGSQL_H
#define PGSQL_H

#include <string>
#include <cstring>
#include <libpq-fe.h>
#include <memory>

PGresult *pgsql_execPrepared( PGconn *sql_conn, const char *stmtName, const int nParams, const char *const * paramValues, const ExecStatusType expect);
void pgsql_CopyData(const char *context, PGconn *sql_conn, const char *sql, int len);
std::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const std::string& sql);
std::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const char *sql);
int pgsql_exec(PGconn *sql_conn, const ExecStatusType expect, const char *fmt, ...)
#ifndef _MSC_VER
 __attribute__ ((format (printf, 3, 4)))
#endif
;

void escape(const std::string &src, std::string& dst);


inline void pgsql_CopyData(const char *context, PGconn *sql_conn, const char *sql) {
    pgsql_CopyData(context, sql_conn, sql, (int) strlen(sql));
}

inline void pgsql_CopyData(const char *context, PGconn *sql_conn, const std::string &sql) {
    pgsql_CopyData(context, sql_conn, sql.c_str(), (int) sql.length());
}
#endif
