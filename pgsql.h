/* Helper functions for pgsql access */

/* Current middle and output-pgsql do a lot of things similarly, this should
 * be used to abstract to commonalities */

//#define DEBUG_PGSQL

PGresult *pgsql_execPrepared( PGconn *sql_conn, const char *stmtName, int nParams, const char *const * paramValues, ExecStatusType expect);
int pgsql_CopyData(const char *context, PGconn *sql_conn, const char *sql);
int pgsql_exec(PGconn *sql_conn, const char *sql, ExecStatusType expect);
void escape(char *out, int len, const char *in);
