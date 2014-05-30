#include "table.hpp"
#include "options.hpp"
#include "util.hpp"

#include <string.h>
#include <boost/format.hpp>

using std::string;
typedef boost::format fmt;

namespace
{
/* Escape data appropriate to the type */
void escape_type(buffer &sql, const char *value, const char *type) {
    int items;

    if (!strcmp(type, "int4")) {
        int from, to;
        /* For integers we take the first number, or the average if it's a-b */
        items = sscanf(value, "%d-%d", &from, &to);
        if (items == 1) {
            sql.printf("%d", from);
        } else if (items == 2) {
            sql.printf("%d", (from + to) / 2);
        } else {
            sql.printf("\\N");
        }
    } else {
        /*
         try to "repair" real values as follows:
         * assume "," to be a decimal mark which need to be replaced by "."
         * like int4 take the first number, or the average if it's a-b
         * assume SI unit (meters)
         * convert feet to meters (1 foot = 0.3048 meters)
         * reject anything else
         */
        if (!strcmp(type, "real")) {
            int i, slen;
            float from, to;

            // we're just using sql as a temporary buffer here.
            sql.cpy(value);

            slen = sql.len();
            for (i = 0; i < slen; i++)
                if (sql.buf[i] == ',')
                    sql.buf[i] = '.';

            items = sscanf(sql.buf, "%f-%f", &from, &to);
            if (items == 1) {
                if ((sql.buf[slen - 2] == 'f') && (sql.buf[slen - 1] == 't')) {
                    from *= 0.3048;
                }
                sql.printf("%f", from);
            } else if (items == 2) {
                if ((sql.buf[slen - 2] == 'f') && (sql.buf[slen - 1] == 't')) {
                    from *= 0.3048;
                    to *= 0.3048;
                }
                sql.printf("%f", (from + to) / 2);
            } else {
                sql.printf("\\N");
            }
        } else {
            escape(sql, value);
        }
    }
}
}

table_t::table_t(const char* name, const char* type, const columns_t& columns, const hstores_t& hstore_columns,
    const int srs, const int scale, const bool append, const bool slim, const bool drop_temp, const int enable_hstore,
    const char* table_space, const char* table_space_index) :
    name(name), type(type), sql_conn(NULL), buflen(0), copyMode(0), srs(srs), scale(scale), append(append), slim(slim),
    drop_temp(drop_temp), enable_hstore(enable_hstore), columns(columns), hstore_columns(hstore_columns),
    table_space(table_space ? table_space : ""), table_space_index(table_space_index ? table_space_index : "")
{
    memset(buffer, 0, sizeof buffer);

    //if we dont have any columns
    if(columns.size() == 0)
    {
        throw std::runtime_error((fmt("No columns provided for table %1%") % name).str());
    }
}

table_t::~table_t()
{
}

void table_t::setup(const char* conninfo)
{
    fprintf(stderr, "Setting up table: %s\n", name.c_str());

    //connect
    PGconn* _conn = PQconnectdb(conninfo);
    if (PQstatus(_conn) != CONNECTION_OK)
        throw std::runtime_error((fmt("Connection to database failed: %1%\n") % PQerrorMessage(_conn)).str());
    sql_conn = _conn;
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");

    //we are making a new table
    if (!append)
    {
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DROP TABLE IF EXISTS %1%") % name).str());
    }//we are checking in append mode that the srid you specified matches whats already there
    else
    {
        //TODO: use pgsql_exec_simple
        string sql = (fmt("SELECT srid FROM geometry_columns WHERE f_table_name='%1%';") % name).str();
        PGresult* res = PQexec(sql_conn, sql.c_str());
        if (!((PQntuples(res) == 1) && (PQnfields(res) == 1)))
            throw std::runtime_error((fmt("Problem reading geometry information for table %1% - does it exist?\n") % name).str());
        int their_srid = atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        if (their_srid != srs)
            throw std::runtime_error((fmt("SRID mismatch: cannot append to table %1% (SRID %2%) using selected SRID %3%\n") % name % their_srid % srs).str());
    }

    /* These _tmp tables can be left behind if we run out of disk space */
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DROP TABLE IF EXISTS %1%_tmp") % name).str());

    begin();
    if (!append) {
        //define the new table
        string sql = (fmt("CREATE TABLE %1% (") % name).str();

        //first with the regular columns
        for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
            sql += (fmt("\"%1%\" %2%, ") % column->first % column->second).str();

        //then with the hstore columns
        for(hstores_t::const_iterator hcolumn = hstore_columns.begin(); hcolumn != hstore_columns.end(); ++hcolumn)
            sql += (fmt("\"%1%\" hstore, ") % (*hcolumn)).str();

        //add tags column
        if (enable_hstore)
            sql += "\"tags\" hstore)";
        //or remove the last ", " from the end
        else
            sql = sql.replace(sql.length() - 2, 2, ")");

        //add the main table space
        if (table_space.length())
            sql += " TABLESPACE " + table_space;

        //create the table
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, sql);

        //add some constraints
        pgsql_exec_simple(sql_conn, PGRES_TUPLES_OK, (fmt("SELECT AddGeometryColumn('%1%', 'way', %2%, '%3%', 2 )") % name % srs % type).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ALTER TABLE %1% ALTER COLUMN way SET NOT NULL") % name).str());

        //slim mode needs this to be able to apply diffs
        if (slim && !drop_temp) {
            sql = (fmt("CREATE INDEX %1%_pkey ON %2% USING BTREE (osm_id)") % name % name).str();
            if (table_space_index.length())
                sql += " TABLESPACE " + table_space_index;
            pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, sql);
        }

    }//appending
    else {
        //check the columns against those in the existing table
        boost::shared_ptr<PGresult> res = pgsql_exec_simple(sql_conn, PGRES_TUPLES_OK, (fmt("SELECT * FROM %1% LIMIT 0") % name).str());
        for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
        {
            if(PQfnumber(res.get(), ('"' + column->first + '"').c_str()) < 0)
            {
#if 0
                throw std::runtime_error((fmt("Append failed. Column \"%1%\" is missing from \"%2%\"\n") % info.name % name).str());
#else
                fprintf(stderr, "%s", (fmt("Adding new column \"%1%\" to \"%2%\"\n") % column->first % name).str().c_str());
                pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ALTER TABLE %1% ADD COLUMN \"%2%\" %3%") % name % column->first % column->second).str());
#endif
            }
            //Note: we do not verify the type or delete unused columns
        }

        //TODO: check over hstore columns

        //TODO? change the type of the geometry column if needed - this can only change to a more permissive type
    }

    //let postgres cache this query as it will presumably happen a lot
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("PREPARE get_wkt (" POSTGRES_OSMID_TYPE ") AS SELECT ST_AsText(way) FROM %1% WHERE osm_id = $1") % name).str());

    //generate column list for COPY
    string cols;
    //first with the regular columns
    for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
        cols += (fmt("\"%1%\", ") % column->first).str();

    //then with the hstore columns
    for(hstores_t::const_iterator hcolumn = hstore_columns.begin(); hcolumn != hstore_columns.end(); ++hcolumn)
        cols += (fmt("\"%1%\", ") % (*hcolumn)).str();

    //add tags column and geom column
    if (enable_hstore)
        cols += "\"tags\", \"way\"";
    //or just the geom column
    else
        cols += "\"way\"";

    //get into copy mode
    copystr = (fmt("COPY %1% (%2%) FROM STDIN") % name % cols).str();
    pgsql_exec_simple(sql_conn, PGRES_COPY_IN, copystr);
    copyMode = 1;
}

void table_t::teardown()
{
    //pgsql_pause_copy();
    if(sql_conn != NULL)
    {
        PQfinish(sql_conn);
        sql_conn = NULL;
    }
}

void table_t::begin()
{
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "BEGIN");
}

void table_t::commit()
{
    pgsql_pause_copy();
    fprintf(stderr, "Committing transaction for %s\n", name.c_str());
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "COMMIT");
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
        pgsql_exec_simple(sql_conn, PGRES_COPY_IN, copystr);
        copyMode = 1;
    }
    /* If the combination of old and new data is too big, flush old data */
    if( (unsigned)(buflen + len) > sizeof( buffer )-10 )
    {
        printf("%s\n%s\n", copystr.c_str(), buffer);
      pgsql_CopyData(name.c_str(), sql_conn, buffer);
      buflen = 0;

      /* If new data by itself is also too big, output it immediately */
      if( (unsigned)len > sizeof( buffer )-10 )
      {
          printf("%s\n%s\n", copystr.c_str(), sql);
        pgsql_CopyData(name.c_str(), sql_conn, sql);
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
        printf("%s\n%s\n", copystr.c_str(), buffer);
      pgsql_CopyData(name.c_str(), sql_conn, buffer);
      buflen = 0;
    }
}

void table_t::pgsql_pause_copy()
{
    PGresult   *res;
    int stop;

    if( !copyMode )
        return;

    //stop the copy
    stop = PQputCopyEnd(sql_conn, NULL);
    if (stop != 1)
       throw std::runtime_error((fmt("stop COPY_END for %1% failed: %2%\n") % name % PQerrorMessage(sql_conn)).str());

    //get the result
    res = PQgetResult(sql_conn);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        PQclear(res);
        throw std::runtime_error((fmt("result COPY_END for %1% failed: %2%\n") % name % PQerrorMessage(sql_conn)).str());
    }
    PQclear(res);
    copyMode = 0;
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
    for(hstores_t::const_iterator hstore_column = hstore_columns.begin(); hstore_column != hstore_columns.end(); ++hstore_column)
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

void table_t::export_tags(struct keyval *tags, struct buffer &sql) {
    //for each column, note we skip the first because its the osmid
    for(columns_t::const_iterator column = columns.begin() + 1; column != columns.end(); ++column)
    {
        struct keyval *tag = NULL;
        if ((tag = getTag(tags, column->first.c_str())))
        {
            escape_type(sql, tag->value, column->second.c_str());
            if (enable_hstore==HSTORE_NORM)
                tag->has_column=1;
        }
        else
            sql.printf("\\N");

        copy_to_table(sql.buf);
        copy_to_table("\t");
    }
}

void table_t::write_way(const osmid_t id, struct keyval *tags, const char *wkt, struct buffer &sql)
{
    //TODO: throw if the type of this table wasnt linestring or geometry

    //add the id
    sql.printf("%" PRIdOSMID "\t", id);
    copy_to_table(sql.buf);

    //get the regular columns' values
    export_tags(tags, sql);

    //get the hstore columns' values
    write_hstore_columns(tags, sql);

    //get the key value pairs for the tags column
    if (enable_hstore)
        write_hstore(tags, sql);

    //give it an srid and put the geom into the copy
    sql.printf("SRID=%d;", srs);
    copy_to_table(sql.buf);
    copy_to_table(wkt);
    copy_to_table("\n");
}

void table_t::write_node(const osmid_t id, struct keyval *tags, double lat, double lon, struct buffer &sql)
{
    //TODO: throw if the type of this table wasnt point or geometry

    //add the id
    sql.printf("%" PRIdOSMID "\t", id);
    copy_to_table(sql.buf);

    //get the regular columns' values
    export_tags(tags, sql);

    //get the hstore columns' values
    write_hstore_columns(tags, sql);

    //get the key value pairs for the tags column
    if (enable_hstore)
        write_hstore(tags, sql);

#ifdef FIXED_POINT
    // guarantee that we use the same values as in the node cache
    lon = util::fix_to_double(util::double_to_fix(lon, scale), scale);
    lat = util::fix_to_double(util::double_to_fix(lat, scale), scale);
#endif

    //give it an srid and put the geom into the copy
    sql.printf("SRID=%d;POINT(%.15g %.15g)", srs, lon, lat);
    copy_to_table(sql.buf);
    copy_to_table("\n");
}

void table_t::delete_row(const osmid_t id)
{
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DELETE FROM %1% WHERE osm_id = %2%") % name % id).str());
}
