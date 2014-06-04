/* Helper functions for pgsql access */

/* Current middle and output-pgsql do a lot of things similarly, this should
 * be used to abstract to commonalities */

#ifndef PGSQL_H
#define PGSQL_H

#include <string>
#include <libpq-fe.h>
#include <boost/shared_ptr.hpp>

PGresult *pgsql_execPrepared( PGconn *sql_conn, const char *stmtName, const int nParams, const char *const * paramValues, const ExecStatusType expect);
void pgsql_CopyData(const char *context, PGconn *sql_conn, const char *sql);
boost::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const std::string& sql);
boost::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const char *sql);
int pgsql_exec(PGconn *sql_conn, const ExecStatusType expect, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));

void escape(const char* src, std::string& dst);
void escape(char *out, int len, const char *in);

#endif
