/* Helper functions for the postgresql connections */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <libpq-fe.h>
#include "osmtypes.h" /* For exit_nicely() */
#include "pgsql.h"

void escape(char *out, int len, const char *in)
{ 
    /* Apply escaping of TEXT COPY data
    Escape: backslash itself, newline, carriage return, and the current delimiter character (tab)
    file:///usr/share/doc/postgresql-8.1.8/html/sql-copy.html
    */
    int count = 0; 
    const char *old_in = in, *old_out = out;

    if (!len)
        return;

    while(*in && count < len-3) { 
        switch(*in) {
            case '\\': *out++ = '\\'; *out++ = '\\'; count+= 2; break;
                /*    case    8: *out++ = '\\'; *out++ = '\b'; count+= 2; break; */
                /*    case   12: *out++ = '\\'; *out++ = '\f'; count+= 2; break; */
            case '\n': *out++ = '\\'; *out++ = '\n'; count+= 2; break;
            case '\r': *out++ = '\\'; *out++ = '\r'; count+= 2; break;
            case '\t': *out++ = '\\'; *out++ = '\t'; count+= 2; break;
                /*    case   11: *out++ = '\\'; *out++ = '\v'; count+= 2; break; */
            default:   *out++ = *in; count++; break;
        }
        in++;
    }
    *out = '\0';

    if (*in)
        fprintf(stderr, "%s truncated at %d chars: %s\n%s\n", __FUNCTION__, count, old_in, old_out);
}

int pgsql_exec(PGconn *sql_conn, ExecStatusType expect, const char *fmt, ...)
{
    PGresult   *res;
    va_list ap;
    char *sql, *nsql;
    int n, size = 100;

    /* Based on vprintf manual page */
    /* Guess we need no more than 100 bytes. */

    if ((sql = malloc(size)) == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit_nicely();
    }

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
        if ((nsql = realloc (sql, size)) == NULL) {
            free(sql);
            fprintf(stderr, "Memory re-allocation failed\n");
            exit_nicely();
        } else {
            sql = nsql;
        }
    }

#ifdef DEBUG_PGSQL
    fprintf( stderr, "Executing: %s\n", sql );
#endif
    res = PQexec(sql_conn, sql);
    if (PQresultStatus(res) != expect) {
        fprintf(stderr, "%s failed: %s\n", sql, PQerrorMessage(sql_conn));
        free(sql);
        PQclear(res);
        exit_nicely();
    }
    free(sql);
    PQclear(res);
    return 0;
}

int pgsql_CopyData(const char *context, PGconn *sql_conn, const char *sql)
{
#ifdef DEBUG_PGSQL
    fprintf( stderr, "%s>>> %s\n", context, sql );
#endif
    int r = PQputCopyData(sql_conn, sql, strlen(sql));
    if (r != 1) {
        fprintf(stderr, "%s - bad result during COPY, data %s\n", context, sql);
        exit_nicely();
    }
    return 0;
}

PGresult *pgsql_execPrepared( PGconn *sql_conn, const char *stmtName, int nParams, const char *const * paramValues, ExecStatusType expect)
{
#ifdef DEBUG_PGSQL
    fprintf( stderr, "ExecPrepared: %s\n", stmtName );
#endif
    PGresult *res = PQexecPrepared(sql_conn, stmtName, nParams, paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != expect) {
        fprintf(stderr, "%s failed: %s(%d)\n", stmtName, PQerrorMessage(sql_conn), PQresultStatus(res));
        if( nParams )
        {
            int i;
            fprintf( stderr, "Arguments were: " );
            for( i=0; i<nParams; i++  )
                fprintf( stderr, "%s, ", paramValues[i] );
            fprintf( stderr,  "\n");
        }
        PQclear(res);
        exit_nicely();
    }
    if( expect != PGRES_TUPLES_OK )
    {
        PQclear(res);
        res = NULL;
    }
    return res;
}

