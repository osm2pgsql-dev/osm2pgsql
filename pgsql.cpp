/* Helper functions for the postgresql connections */
#include "pgsql.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <memory>
#include <boost/format.hpp>

void escape(const std::string &src, std::string &dst)
{
    for (const char c: src) {
        switch(c) {
            case '\\':  dst.append("\\\\"); break;
            //case 8:   dst.append("\\\b"); break;
            //case 12:  dst.append("\\\f"); break;
            case '\n':  dst.append("\\\n"); break;
            case '\r':  dst.append("\\\r"); break;
            case '\t':  dst.append("\\\t"); break;
            //case 11:  dst.append("\\\v"); break;
            default:    dst.push_back(c); break;
        }
    }
}


std::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const std::string& sql)
{
    return pgsql_exec_simple(sql_conn, expect, sql.c_str());
}

std::shared_ptr<PGresult> pgsql_exec_simple(PGconn *sql_conn, const ExecStatusType expect, const char *sql)
{
    PGresult* res;
#ifdef DEBUG_PGSQL
    fprintf( stderr, "Executing: %s\n", sql );
#endif
    res = PQexec(sql_conn, sql);
    if (PQresultStatus(res) != expect) {
        PQclear(res);
        throw std::runtime_error((boost::format("%1% failed: %2%\n") % sql % PQerrorMessage(sql_conn)).str());
    }
    return std::shared_ptr<PGresult>(res, &PQclear);
}

int pgsql_exec(PGconn *sql_conn, const ExecStatusType expect, const char *fmt, ...)
{

    va_list ap;
    char *sql, *nsql;
    int n, size = 100;

    /* Based on vprintf manual page */
    /* Guess we need no more than 100 bytes. */

    if ((sql = (char *)malloc(size)) == nullptr)
        throw std::runtime_error("Memory allocation failed in pgsql_exec");

    while (1) {
        /* Try to print in the allocated space. */
        va_start(ap, fmt);
        n = vsnprintf(sql, size, fmt, ap);
        va_end(ap);
        /* If that worked, return the string. */
        if (n > -1 && n < size)
            break;
        /* Else try again with more space. */
        if (n > -1)    /* glibc 2.1 */
            size = n+1; /* precisely what is needed */
        else           /* glibc 2.0 */
            size *= 2;  /* twice the old size */
        if ((nsql = (char *)realloc (sql, size)) == nullptr) {
            free(sql);
            throw std::runtime_error("Memory re-allocation failed in pgsql_exec");
        } else {
            sql = nsql;
        }
    }

#ifdef DEBUG_PGSQL
    fprintf( stderr, "Executing: %s\n", sql );
#endif
    PGresult* res = PQexec(sql_conn, sql);
    if (PQresultStatus(res) != expect) {
        std::string err_msg = (boost::format("%1% failed: %2%") % sql % PQerrorMessage(sql_conn)).str();
        free(sql);
        PQclear(res);
        throw std::runtime_error(err_msg);
    }
    free(sql);
    PQclear(res);
    return 0;
}

void pgsql_CopyData(const char *context, PGconn *sql_conn, const char *sql, int len)
{
#ifdef DEBUG_PGSQL
    fprintf(stderr, "%s>>> %s\n", context, sql );
#endif
    int r = PQputCopyData(sql_conn, sql, len);
    switch(r)
    {
        //need to wait for write ready
        case 0:
            throw std::runtime_error((boost::format("%1% - bad result during COPY, data %2%") % context % sql % PQerrorMessage(sql_conn)).str());
            break;
        //error occurred
        case -1:
            throw std::runtime_error((boost::format("%1%: %2% - bad result during COPY, data %3%") % PQerrorMessage(sql_conn) % context % sql).str());
            break;
        //other possibility is 1 which means success
    }
}

PGresult *pgsql_execPrepared( PGconn *sql_conn, const char *stmtName, const int nParams, const char *const * paramValues, const ExecStatusType expect)
{
#ifdef DEBUG_PGSQL
    fprintf( stderr, "ExecPrepared: %s\n", stmtName );
#endif
    //run the prepared statement
    PGresult *res = PQexecPrepared(sql_conn, stmtName, nParams, paramValues, nullptr, nullptr, 0);
    if(PQresultStatus(res) != expect)
    {
        std::string message = (boost::format("%1% failed: %2%(%3%)\n") % stmtName % PQerrorMessage(sql_conn) % PQresultStatus(res)).str();
        if(nParams)
        {
             message += "Arguments were: ";
            for(int i = 0; i < nParams; i++)
            {
                message += paramValues[i];
                message += ", ";
            }
        }
        PQclear(res);
        throw std::runtime_error(message);
    }

    //TODO: this seems a bit strange
    //if you decided you wanted to expect something other than this you didnt want to use the result?
    if( expect != PGRES_TUPLES_OK )
    {
        PQclear(res);
        res = nullptr;
    }
    return res;
}
