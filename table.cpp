#include "table.hpp"
#include "options.hpp"
#include "util.hpp"

#include <string.h>
#include <boost/format.hpp>

using std::string;
typedef boost::format fmt;

#define BUFFER_SEND_SIZE 1024

namespace
{

void escape(const char* src, string& dst)
{
    //loop over the chars in the input
    size_t length = strlen(src);
    for(size_t i = 0; i < length; ++i)
    {
        switch(src[i]) {
            case '\\':  dst.append("\\\\"); break;
            //case 8:   dst.append("\\\b"); break;
            //case 12:  dst.append("\\\f"); break;
            case '\n':  dst.append("\\\n"); break;
            case '\r':  dst.append("\\\r"); break;
            case '\t':  dst.append("\\\t"); break;
            //case 11:  dst.append("\\\v"); break;
            default:    dst.push_back(src[i]); break;
        }
    }
}


//create an escaped version of the string for hstore table insert
void escape4hstore(const char *src, string& dst)
{
    dst.push_back('"');
    for (size_t i = 0; i < strlen(src); ++i) {
        switch (src[i]) {
            case '\\':
                dst.append("\\\\\\\\");
                break;
            case '"':
                dst.append("\\\\\"");
                break;
            case '\t':
                dst.append("\\\t");
                break;
            case '\r':
                dst.append("\\\r");
                break;
            case '\n':
                dst.append("\\\n");
                break;
            default:
                dst.push_back(src[i]);
                break;
        }
    }
    dst.push_back('"');
}

/* Escape data appropriate to the type */
void escape_type(const char *value, const char *type, string& dst) {

    // For integers we take the first number, or the average if it's a-b
    if (!strcmp(type, "int4")) {
        int from, to;
        int items = sscanf(value, "%d-%d", &from, &to);
        if (items == 1)
            dst.append((fmt("%1%") % from).str());
        else if (items == 2)
            dst.append((fmt("%1%") % ((from + to) / 2)).str());
        else
            dst.append("\\N");
    }
        /* try to "repair" real values as follows:
         * assume "," to be a decimal mark which need to be replaced by "."
         * like int4 take the first number, or the average if it's a-b
         * assume SI unit (meters)
         * convert feet to meters (1 foot = 0.3048 meters)
         * reject anything else
         */
    else if (!strcmp(type, "real"))
    {
        string escaped(value);
        std::replace(escaped.begin(), escaped.end(), ',', '.');

        float from, to;
        int items = sscanf(escaped.c_str(), "%f-%f", &from, &to);
        if (items == 1)
        {
            if (escaped.substr(escaped.size() - 2).compare("ft") == 0)
                from *= 0.3048;
            dst.append((fmt("%1%") % from).str());
        }
        else if (items == 2)
        {
            if (escaped.substr(escaped.size() - 2).compare("ft") == 0)
            {
                from *= 0.3048;
                to *= 0.3048;
            }
            dst.append((fmt("%1%") % ((from + to) / 2)).str());
        }
        else
            dst.append("\\N");
    }//just a string
    else
        escape(value, dst);
}
}

table_t::table_t(const string& name, const string& type, const columns_t& columns, const hstores_t& hstore_columns,
    const int srid, const int scale, const bool append, const bool slim, const bool drop_temp, const int hstore_mode,
    const bool enable_hstore_index, const boost::optional<string>& table_space, const boost::optional<string>& table_space_index) :
    name(name), type(type), sql_conn(NULL), copyMode(false), srid(srid), scale(scale), append(append), slim(slim),
    drop_temp(drop_temp), hstore_mode(hstore_mode), enable_hstore_index(enable_hstore_index), columns(columns),
    hstore_columns(hstore_columns), table_space(table_space), table_space_index(table_space_index)
{
    //if we dont have any columns
    if(columns.size() == 0)
    {
        throw std::runtime_error((fmt("No columns provided for table %1%") % name).str());
    }

    //nothing to copy to start with
    buffer = "";
}

table_t::~table_t()
{
    teardown();
}

void table_t::setup(const string& conninfo)
{
    fprintf(stderr, "Setting up table: %s\n", name.c_str());

    //connect
    PGconn* _conn = PQconnectdb(conninfo.c_str());
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
        boost::shared_ptr<PGresult> res =  pgsql_exec_simple(sql_conn, PGRES_TUPLES_OK, (fmt("SELECT srid FROM geometry_columns WHERE f_table_name='%1%';") % name).str());
        if (!((PQntuples(res.get()) == 1) && (PQnfields(res.get()) == 1)))
            throw std::runtime_error((fmt("Problem reading geometry information for table %1% - does it exist?\n") % name).str());
        int their_srid = atoi(PQgetvalue(res.get(), 0, 0));
        if (their_srid != srid)
            throw std::runtime_error((fmt("SRID mismatch: cannot append to table %1% (SRID %2%) using selected SRID %3%\n") % name % their_srid % srid).str());
    }

    /* These _tmp tables can be left behind if we run out of disk space */
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DROP TABLE IF EXISTS %1%_tmp") % name).str());

    begin();

    //making a new table
    if (!append)
    {
        //define the new table
        string sql = (fmt("CREATE TABLE %1% (osm_id %2%,") % name % POSTGRES_OSMID_TYPE).str();

        //first with the regular columns
        for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
            sql += (fmt("\"%1%\" %2%,") % column->first % column->second).str();

        //then with the hstore columns
        for(hstores_t::const_iterator hcolumn = hstore_columns.begin(); hcolumn != hstore_columns.end(); ++hcolumn)
            sql += (fmt("\"%1%\" hstore,") % (*hcolumn)).str();

        //add tags column
        if (hstore_mode != HSTORE_NONE)
            sql += "\"tags\" hstore)";
        //or remove the last ", " from the end
        else
            sql[sql.length() - 1] = ')';

        //add the main table space
        if (table_space)
            sql += " TABLESPACE " + table_space.get();

        //create the table
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, sql);

        //add some constraints
        pgsql_exec_simple(sql_conn, PGRES_TUPLES_OK, (fmt("SELECT AddGeometryColumn('%1%', 'way', %2%, '%3%', 2 )") % name % srid % type).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ALTER TABLE %1% ALTER COLUMN way SET NOT NULL") % name).str());

        //slim mode needs this to be able to apply diffs
        if (slim && !drop_temp) {
            sql = (fmt("CREATE INDEX %1%_pkey ON %2% USING BTREE (osm_id)") % name % name).str();
            if (table_space_index)
                sql += " TABLESPACE " + table_space_index.get();
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

        //TODO: change the type of the geometry column if needed - this can only change to a more permissive type
    }

    //let postgres cache this query as it will presumably happen a lot
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("PREPARE get_wkt (" POSTGRES_OSMID_TYPE ") AS SELECT ST_AsText(way) FROM %1% WHERE osm_id = $1") % name).str());

    //generate column list for COPY
    string cols = "osm_id,";
    //first with the regular columns
    for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
        cols += (fmt("\"%1%\",") % column->first).str();

    //then with the hstore columns
    for(hstores_t::const_iterator hcolumn = hstore_columns.begin(); hcolumn != hstore_columns.end(); ++hcolumn)
        cols += (fmt("\"%1%\",") % (*hcolumn)).str();

    //add tags column and geom column
    if (hstore_mode != HSTORE_NONE)
        cols += "tags,way";
    //or just the geom column
    else
        cols += "way";

    //get into copy mode
    copystr = (fmt("COPY %1% (%2%) FROM STDIN") % name % cols).str();
    pgsql_exec_simple(sql_conn, PGRES_COPY_IN, copystr);
    copyMode = true;
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

void table_t::pgsql_pause_copy()
{
    PGresult* res;
    int stop;

    //we werent copying anyway
    if(!copyMode)
        return;
    //if there is stuff left over in the copy buffer send it offand copy it before we stop
    else if(buffer.length() != 0)
    {
        pgsql_CopyData(name.c_str(), sql_conn, buffer.c_str());
        buffer.clear();
    };

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
    copyMode = false;
}

void table_t::write_tags_column(keyval *tags, std::string& values)
{
    //iterate through the list of tags, first one is always null
    bool added = false;
    for (keyval* xtags = tags->next; xtags->key != NULL; xtags = xtags->next)
    {
        //skip z_order tag and keys which have their own column
        if ((xtags->has_column) || (strcmp("z_order", xtags->key) == 0))
            continue;

        //hstore ASCII representation looks like "key"=>"value"
        if(added)
            values.push_back(',');
        escape4hstore(xtags->key, values);
        values.append("=>");
        escape4hstore(xtags->value, values);

        //we did at least one so we need commas from here on out
        added = true;
    }

    //finish the hstore column by placing a TAB into the data stream
    values.push_back('\t');
}

/* write an hstore column to the database */
void table_t::write_hstore_columns(keyval *tags, std::string& values)
{
    //iterate over all configured hstore columns in the options
    for(hstores_t::const_iterator hstore_column = hstore_columns.begin(); hstore_column != hstore_columns.end(); ++hstore_column)
    {
        //a clone of the tags pointer
        keyval* xtags = tags;
        bool added = false;

        //iterate through the list of tags, first one is always null
        for (keyval* xtags = tags->next; xtags->key != NULL; xtags = xtags->next)
        {
            //check if the tag's key starts with the name of the hstore column
            if(hstore_column->find(xtags->key) == 0)
            {
                //generate the short key name, somehow pointer arithmetic works against this member of the keyval data structure...
                char* shortkey = xtags->key + hstore_column->size();

                //and pack the shortkey with its value into the hstore
                //hstore ASCII representation looks like "key"=>"value"
                if(added)
                    values.push_back(',');
                escape4hstore(shortkey, values);
                values.append("=>");
                escape4hstore(xtags->value, values);

                //we did at least one so we need commas from here on out
                added = true;
            }
        }

        //if you found not matching tags write a NULL
        if(!added)
            values.append("\\N");

        //finish the column off with a tab
        values.push_back('\t');
    }
}

void table_t::write_columns(keyval *tags, string& values)
{
    //for each column
    for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
    {
        keyval *tag = NULL;
        if ((tag = getTag(tags, column->first.c_str())))
        {
            escape_type(tag->value, column->second.c_str(), values);
            //remember we already used this one so we cant use again later in the hstore column
            if (hstore_mode == HSTORE_NORM)
                tag->has_column = 1;
        }
        else
            values.append("\\N");
        values.push_back('\t');
    }
}

void table_t::write_wkt(const osmid_t id, struct keyval *tags, const char *wkt)
{
    //add the osm id
    buffer.append((fmt("%1%\t") % id).str());

    //get the regular columns' values
    write_columns(tags, buffer);

    //get the hstore columns' values
    write_hstore_columns(tags, buffer);

    //get the key value pairs for the tags column
    if (hstore_mode != HSTORE_NONE)
        write_tags_column(tags, buffer);

    //give the wkt an srid
    buffer.append((fmt("SRID=%1%;") % srid).str());
    //add the wkt
    buffer.append(wkt);
    //we need \n because we are copying from stdin
    buffer.push_back('\n');

    //tell the db we are copying if for some reason we arent already
    if (!copyMode)
    {
        pgsql_exec_simple(sql_conn, PGRES_COPY_IN, copystr);
        copyMode = true;
    }

    //send all the data to postgres
    if(buffer.length() > BUFFER_SEND_SIZE)
    {
        pgsql_CopyData(name.c_str(), sql_conn, buffer.c_str());
        buffer.clear();
    }
}

void table_t::write_node(const osmid_t id, struct keyval *tags, double lat, double lon)
{

#ifdef FIXED_POINT
    // guarantee that we use the same values as in the node cache
    lon = util::fix_to_double(util::double_to_fix(lon, scale), scale);
    lat = util::fix_to_double(util::double_to_fix(lat, scale), scale);
#endif

    write_wkt(id, tags, (fmt("POINT(%.15g %.15g)") % lon % lat).str().c_str());
}

void table_t::delete_row(const osmid_t id)
{
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DELETE FROM %1% WHERE osm_id = %2%") % name % id).str());
}

table_t::wkts::wkts(PGresult* result):result(result), current(0)
{
    count = PQntuples(result);
}

table_t::wkts::~wkts()
{
    PQclear(result);
}

const char* table_t::wkts::get_next()
{
    if(current < count)
        return PQgetvalue(result, current++, 0);
    return NULL;
}

size_t table_t::wkts::get_count() const
{
    return count;
}

void table_t::wkts::reset()
{
    //NOTE: PQgetvalue doc doesn't say if you can call it multiple times with the same row col
    current = 0;
}

boost::shared_ptr<table_t::wkts> table_t::get_wkts(const osmid_t id)
{

    char const *paramValues[1];
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    //the prepared statement get_wkt will behave differently depending on the sql_conn
    //each table has its own sql_connection with the get_way referring to the appropriate table
    PGresult* res = pgsql_execPrepared(sql_conn, "get_wkt", 1, (const char * const *)paramValues, PGRES_TUPLES_OK);
    return boost::shared_ptr<wkts>(new wkts(res));
}

void table_t::stop()
{
    pgsql_pause_copy();
    if (!append)
    {
        time_t start, end;
        time(&start);

        fprintf(stderr, "Sorting data and creating indexes for %s\n", name.c_str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ANALYZE %1%") % name).str());
        fprintf(stderr, "Analyzing %s finished\n", name.c_str());

        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE TABLE %1%_tmp %2% AS SELECT * FROM %3% ORDER BY way") % name % (table_space ? "TABLESPACE " + table_space.get() : "") % name).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DROP TABLE %1%") % name).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ALTER TABLE %1%_tmp RENAME TO %2%") % name % name).str());
        fprintf(stderr, "Copying %s to cluster by geometry finished\n", name.c_str());
        fprintf(stderr, "Creating geometry index on  %s\n", name.c_str());

        // Use fillfactor 100 for un-updatable imports
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_index ON %2% USING GIST (way) %3% %4%") % name % name %
            (slim && !drop_temp ? "" : "WITH (FILLFACTOR=100)") %
            (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());

        /* slim mode needs this to be able to apply diffs */
        if (slim && !drop_temp)
        {
            fprintf(stderr, "Creating osm_id index on  %s\n", name.c_str());
            pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_pkey ON %2% USING BTREE (osm_id) %3%") % name % name %
                (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());
        }
        /* Create hstore index if selected */
        if (enable_hstore_index) {
            fprintf(stderr, "Creating hstore indexes on  %s\n", name.c_str());
            if (hstore_mode != HSTORE_NONE) {
                pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_tags_index ON %2% USING GIN (tags) %3%") % name % name %
                    (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());
            }
            for(size_t i = 0; i < hstore_columns.size(); ++i) {
                pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_hstore_%2%_index ON %3% USING GIN (\"%4%\") %5%") % name % i % name % hstore_columns[i] %
                    (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());
            }
        }
        fprintf(stderr, "Creating indexes on  %s finished\n", name.c_str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("GRANT SELECT ON %1% TO PUBLIC") % name).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ANALYZE %1%") % name).str());
        time(&end);
        fprintf(stderr, "All indexes on  %s created  in %ds\n", name.c_str(), (int)(end - start));
    }
    teardown();

    fprintf(stderr, "Completed %s\n", name.c_str());
}
