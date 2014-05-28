#include "table.hpp"
#include "options.hpp"
#include "util.hpp"

#include <string.h>

namespace
{

}

table_t::table_t(const char *name_, const char *type_, const int srs, const int enable_hstore, const std::vector<std::string>& hstore_columns)
    : name(strdup(name_)), type(type_),
      sql_conn(NULL), buflen(0), copyMode(0),
      columns(NULL), srs(srs), enable_hstore(enable_hstore), hstore_columns(hstore_columns)
{
    memset(buffer, 0, sizeof buffer);
}

table_t::~table_t()
{
    free(name);
    free(columns);
}

/* Handles copying out, but coalesces the data into large chunks for
 * efficiency. PostgreSQL doesn't actually need this, but each time you send
 * a block of data you get 5 bytes of overhead. Since we go column by column
 * with most empty and one byte delimiters, without this optimisation we
 * transfer three times the amount of data necessary.
 */
void table_t::copy_to_table(const char *sql)
{
    unsigned int len = strlen(sql);

    /* Return to copy mode if we dropped out */
    if( !copyMode )
    {
        pgsql_exec(sql_conn, PGRES_COPY_IN, "COPY %s (%s,way) FROM STDIN", name, columns);
        copyMode = 1;
    }
    /* If the combination of old and new data is too big, flush old data */
    if( (unsigned)(buflen + len) > sizeof( buffer )-10 )
    {
      pgsql_CopyData(name, sql_conn, buffer);
      buflen = 0;

      /* If new data by itself is also too big, output it immediately */
      if( (unsigned)len > sizeof( buffer )-10 )
      {
        pgsql_CopyData(name, sql_conn, sql);
        len = 0;
      }
    }
    /* Normal case, just append to buffer */
    if( len > 0 )
    {
      strcpy( buffer+buflen, sql );
      buflen += len;
      len = 0;
    }

    /* If we have completed a line, output it */
    if( buflen > 0 && buffer[buflen-1] == '\n' )
    {
      pgsql_CopyData(name, sql_conn, buffer);
      buflen = 0;
    }
}

void table_t::write_hstore(keyval *tags, struct buffer &sql)
{
    size_t hlen;
    /* a clone of the tags pointer */
    struct keyval *xtags = tags;

    /* while this tags has a follow-up.. */
    while (xtags->next->key != NULL)
    {

      /* hard exclude z_order tag and keys which have their own column */
      if ((xtags->next->has_column) || (strcmp("z_order",xtags->next->key)==0)) {
          /* update the tag-pointer to point to the next tag */
          xtags = xtags->next;
          continue;
      }

      /*
        hstore ASCII representation looks like
        "<key>"=>"<value>"

        we need at least strlen(key)+strlen(value)+6+'\0' bytes
        in theory any single character could also be escaped
        thus we need an additional factor of 2.
        The maximum lenght of a single hstore element is thus
        calcuated as follows:
      */
      hlen=2 * (strlen(xtags->next->key) + strlen(xtags->next->value)) + 7;

      /* if the sql buffer is too small */
      if (hlen > sql.capacity()) {
        sql.reserve(hlen);
      }

      /* pack the tag with its value into the hstore */
      keyval2hstore(sql, xtags->next);
      copy_to_table(sql.buf);

      /* update the tag-pointer to point to the next tag */
      xtags = xtags->next;

      /* if the tag has a follow up, add a comma to the end */
      if (xtags->next->key != NULL)
          copy_to_table(",");
    }

    /* finish the hstore column by placing a TAB into the data stream */
    copy_to_table("\t");

    /* the main hstore-column has now been written */
}

/* write an hstore column to the database */
void table_t::write_hstore_columns(keyval *tags, struct buffer &sql)
{
    char *shortkey;
    /* the index of the current hstore column */
    int i_hstore_column;
    int found;
    struct keyval *xtags;
    char *pos;
    size_t hlen;

    /* iterate over all configured hstore colums in the options */
    for(std::vector<std::string>::const_iterator hstore_column = hstore_columns.begin(); hstore_column != hstore_columns.end(); ++hstore_column)
    {
        /* did this node have a tag that matched the current hstore column */
        found = 0;

        /* a clone of the tags pointer */
        xtags = tags;

        /* while this tags has a follow-up.. */
        while (xtags->next->key != NULL) {

            /* check if the tag's key starts with the name of the hstore column */
            pos = strstr(xtags->next->key, hstore_column->c_str());

            /* and if it does.. */
            if(pos == xtags->next->key)
            {
                /* remember we found one */
                found=1;

                /* generate the short key name */
                shortkey = xtags->next->key + hstore_column->size();

                /* calculate the size needed for this hstore entry */
                hlen=2*(strlen(shortkey)+strlen(xtags->next->value))+7;

                /* if the sql buffer is too small */
                if (hlen > sql.capacity()) {
                    /* resize it */
                    sql.reserve(hlen);
                }

                /* and pack the shortkey with its value into the hstore */
                keyval2hstore_manual(sql, shortkey, xtags->next->value);
                copy_to_table(sql.buf);

                /* update the tag-pointer to point to the next tag */
                xtags=xtags->next;

                /* if the tag has a follow up, add a comma to the end */
                if (xtags->next->key != NULL)
                    copy_to_table(",");
            }
            else
            {
                /* update the tag-pointer to point to the next tag */
                xtags=xtags->next;
            }
        }

        /* if no matching tag has been found, write a NULL */
        if(!found)
            copy_to_table("\\N");

        /* finish the hstore column by placing a TAB into the data stream */
        copy_to_table("\t");
    }

    /* all hstore-columns have now been written */
}

void table_t::pgsql_pause_copy()
{
    PGresult   *res;
    int stop;

    if( !copyMode )
        return;

    /* Terminate any pending COPY */
    stop = PQputCopyEnd(sql_conn, NULL);
    if (stop != 1) {
       fprintf(stderr, "COPY_END for %s failed: %s\n", name, PQerrorMessage(sql_conn));
       util::exit_nicely();
    }

    res = PQgetResult(sql_conn);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
       fprintf(stderr, "COPY_END for %s failed: %s\n", name, PQerrorMessage(sql_conn));
       PQclear(res);
       util::exit_nicely();
    }
    PQclear(res);
    copyMode = 0;
}

void table_t::connect(const char* conninfo)
{
    PGconn* _conn = PQconnectdb(conninfo);

    /* Check to see that the backend connection was successfully made */
    if (PQstatus(_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(_conn));
        util::exit_nicely();
    }
    sql_conn = _conn;
    pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");
}

void table_t::disconnect()
{
    //pgsql_pause_copy();
    if(sql_conn != NULL)
    {
        PQfinish(sql_conn);
        sql_conn = NULL;
    }
}

void table_t::commit()
{
    pgsql_pause_copy();
    fprintf(stderr, "Committing transaction for %s\n", name);
    pgsql_exec(sql_conn, PGRES_COMMAND_OK, "COMMIT");
}
