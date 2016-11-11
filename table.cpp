#include "table.hpp"
#include "options.hpp"
#include "util.hpp"

#include <exception>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <utility>
#include <time.h>

using std::string;
typedef boost::format fmt;

#define BUFFER_SEND_SIZE 1024


table_t::table_t(const string& conninfo, const string& name, const string& type, const columns_t& columns, const hstores_t& hstore_columns,
    const int srid, const bool append, const bool slim, const bool drop_temp, const int hstore_mode,
    const bool enable_hstore_index, const boost::optional<string>& table_space, const boost::optional<string>& table_space_index) :
    conninfo(conninfo), name(name), type(type), sql_conn(nullptr), copyMode(false), srid((fmt("%1%") % srid).str()),
    append(append), slim(slim), drop_temp(drop_temp), hstore_mode(hstore_mode), enable_hstore_index(enable_hstore_index),
    columns(columns), hstore_columns(hstore_columns), table_space(table_space), table_space_index(table_space_index)
{
    //if we dont have any columns
    if(columns.size() == 0 && hstore_mode != HSTORE_ALL)
        throw std::runtime_error((fmt("No columns provided for table %1%") % name).str());

    //nothing to copy to start with
    buffer = "";

    //we use these a lot, so instead of constantly allocating them we predefine these
    single_fmt = fmt("%1%");
    point_fmt = fmt("POINT(%.15g %.15g)");
    del_fmt = fmt("DELETE FROM %1% WHERE osm_id = %2%");
}

table_t::table_t(const table_t& other):
    conninfo(other.conninfo), name(other.name), type(other.type), sql_conn(nullptr), copyMode(false), buffer(), srid(other.srid),
    append(other.append), slim(other.slim), drop_temp(other.drop_temp), hstore_mode(other.hstore_mode), enable_hstore_index(other.enable_hstore_index),
    columns(other.columns), hstore_columns(other.hstore_columns), copystr(other.copystr), table_space(other.table_space),
    table_space_index(other.table_space_index), single_fmt(other.single_fmt), point_fmt(other.point_fmt), del_fmt(other.del_fmt)
{
    // if the other table has already started, then we want to execute
    // the same stuff to get into the same state. but if it hasn't, then
    // this would be premature.
    if (other.sql_conn) {
        connect();
        //let postgres cache this query as it will presumably happen a lot
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("PREPARE get_wkb (" POSTGRES_OSMID_TYPE ") AS SELECT way FROM %1% WHERE osm_id = $1") % name).str());
        //start the copy
        begin();
        pgsql_exec_simple(sql_conn, PGRES_COPY_IN, copystr);
        copyMode = true;
    }
}

table_t::~table_t()
{
    teardown();
}

std::string const& table_t::get_name() {
    return name;
}

void table_t::teardown()
{
    if(sql_conn != nullptr)
    {
        PQfinish(sql_conn);
        sql_conn = nullptr;
    }
}

void table_t::begin()
{
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "BEGIN");
}

void table_t::commit()
{
    stop_copy();
    fprintf(stderr, "Committing transaction for %s\n", name.c_str());
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "COMMIT");
}

void table_t::connect()
{
    //connect
    PGconn* _conn = PQconnectdb(conninfo.c_str());
    if (PQstatus(_conn) != CONNECTION_OK)
        throw std::runtime_error((fmt("Connection to database failed: %1%\n") % PQerrorMessage(_conn)).str());
    sql_conn = _conn;
    //let commits happen faster by delaying when they actually occur
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");
}

void table_t::start()
{
    if(sql_conn)
        throw std::runtime_error(name + " cannot start, its already started");

    connect();
    fprintf(stderr, "Setting up table: %s\n", name.c_str());
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "SET client_min_messages = WARNING");
    //we are making a new table
    if (!append)
    {
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DROP TABLE IF EXISTS %1%") % name).str());
    }

    /* These _tmp tables can be left behind if we run out of disk space */
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DROP TABLE IF EXISTS %1%_tmp") % name).str());

    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "RESET client_min_messages");
    begin();

    //making a new table
    if (!append)
    {
        //define the new table
        string sql = (fmt("CREATE UNLOGGED TABLE %1% (osm_id %2%,") % name % POSTGRES_OSMID_TYPE).str();

        //first with the regular columns
        for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
            sql += (fmt("\"%1%\" %2%,") % column->first % column->second).str();

        //then with the hstore columns
        for(hstores_t::const_iterator hcolumn = hstore_columns.begin(); hcolumn != hstore_columns.end(); ++hcolumn)
            sql += (fmt("\"%1%\" hstore,") % (*hcolumn)).str();

        //add tags column
        if (hstore_mode != HSTORE_NONE) {
            sql += "\"tags\" hstore,";
        }

        sql += (fmt("way geometry(%1%,%2%) )") % type % srid).str();

        // The final tables are created with CREATE TABLE AS ... SELECT * FROM ...
        // This means that they won't get this autovacuum setting, so it doesn't
        // doesn't need to be RESET on these tables
        sql += " WITH ( autovacuum_enabled = FALSE )";
        //add the main table space
        if (table_space)
            sql += " TABLESPACE " + table_space.get();

        //create the table
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, sql);

        //slim mode needs this to be able to delete from tables in pending
        if (slim && !drop_temp) {
            sql = (fmt("CREATE INDEX %1%_pkey ON %1% USING BTREE (osm_id)") % name).str();
            if (table_space_index)
                sql += " TABLESPACE " + table_space_index.get();
            pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, sql);
        }

    }//appending
    else {
        //check the columns against those in the existing table
        std::shared_ptr<PGresult> res = pgsql_exec_simple(sql_conn, PGRES_TUPLES_OK, (fmt("SELECT * FROM %1% LIMIT 0") % name).str());
        for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
        {
            if(PQfnumber(res.get(), ('"' + column->first + '"').c_str()) < 0)
            {
#if 0
                throw std::runtime_error((fmt("Append failed. Column \"%1%\" is missing from \"%1%\"\n") % info.name).str());
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
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("PREPARE get_wkb (" POSTGRES_OSMID_TYPE ") AS SELECT way FROM %1% WHERE osm_id = $1") % name).str());

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

void table_t::stop()
{
    stop_copy();
    if (!append)
    {
        time_t start, end;
        time(&start);

        fprintf(stderr, "Sorting data and creating indexes for %s\n", name.c_str());

        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE TABLE %1%_tmp %2% AS SELECT * FROM %1% ORDER BY ST_GeoHash(ST_Transform(ST_Envelope(way),4326),10) COLLATE \"C\"") % name % (table_space ? "TABLESPACE " + table_space.get() : "")).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("DROP TABLE %1%") % name).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ALTER TABLE %1%_tmp RENAME TO %1%") % name).str());
        fprintf(stderr, "Copying %s to cluster by geometry finished\n", name.c_str());
        fprintf(stderr, "Creating geometry index on %s\n", name.c_str());

        // Use fillfactor 100 for un-updatable imports
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_index ON %1% USING GIST (way) %2% %3%") % name %
            (slim && !drop_temp ? "" : "WITH (FILLFACTOR=100)") %
            (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());

        /* slim mode needs this to be able to apply diffs */
        if (slim && !drop_temp)
        {
            fprintf(stderr, "Creating osm_id index on %s\n", name.c_str());
            pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_pkey ON %1% USING BTREE (osm_id) %2%") % name %
                (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());
        }
        /* Create hstore index if selected */
        if (enable_hstore_index) {
            fprintf(stderr, "Creating hstore indexes on %s\n", name.c_str());
            if (hstore_mode != HSTORE_NONE) {
                pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_tags_index ON %1% USING GIN (tags) %2%") % name %
                    (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());
            }
            for(size_t i = 0; i < hstore_columns.size(); ++i) {
                pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("CREATE INDEX %1%_hstore_%2%_index ON %1% USING GIN (\"%3%\") %4%") % name % i % hstore_columns[i] %
                    (table_space_index ? "TABLESPACE " + table_space_index.get() : "")).str());
            }
        }
        fprintf(stderr, "Creating indexes on %s finished\n", name.c_str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("GRANT SELECT ON %1% TO PUBLIC") % name).str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ANALYZE %1%") % name).str());
        time(&end);
        fprintf(stderr, "All indexes on %s created in %ds\n", name.c_str(), (int)(end - start));
    }
    teardown();

    fprintf(stderr, "Completed %s\n", name.c_str());
}

void table_t::stop_copy()
{
    PGresult* res;
    int stop;

    //we werent copying anyway
    if(!copyMode)
        return;
    //if there is stuff left over in the copy buffer send it offand copy it before we stop
    else if(buffer.length() != 0)
    {
        pgsql_CopyData(name.c_str(), sql_conn, buffer);
        buffer.clear();
    };

    //stop the copy
    stop = PQputCopyEnd(sql_conn, nullptr);
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

void table_t::write_node(const osmid_t id, const taglist_t &tags, double lat, double lon)
{
    write_row(id, tags, (point_fmt % lon % lat).str());
}

void table_t::delete_row(const osmid_t id)
{
    stop_copy();
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (del_fmt % name % id).str());
}

void table_t::write_row(const osmid_t id, const taglist_t &tags, const std::string &geom)
{
    //add the osm id
    buffer.append((single_fmt % id).str());
    buffer.push_back('\t');

    // used to remember which columns have been written out already.
    std::vector<bool> used;

    if (hstore_mode != HSTORE_NONE)
        used.assign(tags.size(), false);

    //get the regular columns' values
    write_columns(tags, buffer, hstore_mode == HSTORE_NORM?&used:nullptr);

    //get the hstore columns' values
    write_hstore_columns(tags, buffer);

    //get the key value pairs for the tags column
    if (hstore_mode != HSTORE_NONE)
        write_tags_column(tags, buffer, used);

    //give the geometry an srid
    buffer.append("SRID=");
    buffer.append(srid);
    buffer.push_back(';');
    //add the geometry
    buffer.append(geom);
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
        pgsql_CopyData(name.c_str(), sql_conn, buffer);
        buffer.clear();
    }
}

void table_t::write_columns(const taglist_t &tags, string& values, std::vector<bool> *used)
{
    //for each column
    for(columns_t::const_iterator column = columns.begin(); column != columns.end(); ++column)
    {
        int idx;
        if ((idx = tags.indexof(column->first)) >= 0)
        {
            escape_type(tags[idx].value, column->second, values);
            //remember we already used this one so we cant use again later in the hstore column
            if (used)
                (*used)[idx] = true;
        }
        else
            values.append("\\N");
        values.push_back('\t');
    }
}

void table_t::write_tags_column(const taglist_t &tags, std::string& values,
                                const std::vector<bool> &used)
{
    //iterate through the list of tags, first one is always null
    bool added = false;
    for (size_t i = 0; i < tags.size(); ++i)
    {
        const tag_t& xtag = tags[i];
        //skip z_order tag and keys which have their own column
        if (used[i] || ("z_order" == xtag.key))
            continue;

        //hstore ASCII representation looks like "key"=>"value"
        if(added)
            values.push_back(',');
        escape4hstore(xtag.key.c_str(), values);
        values.append("=>");
        escape4hstore(xtag.value.c_str(), values);

        //we did at least one so we need commas from here on out
        added = true;
    }

    //finish the hstore column by placing a TAB into the data stream
    values.push_back('\t');
}

/* write an hstore column to the database */
void table_t::write_hstore_columns(const taglist_t &tags, std::string& values)
{
    //iterate over all configured hstore columns in the options
    for(hstores_t::const_iterator hstore_column = hstore_columns.begin(); hstore_column != hstore_columns.end(); ++hstore_column)
    {
        bool added = false;

        //iterate through the list of tags, first one is always null
        for (taglist_t::const_iterator xtags = tags.begin(); xtags != tags.end(); ++xtags)
        {
            //check if the tag's key starts with the name of the hstore column
            if(xtags->key.compare(0, hstore_column->size(), *hstore_column) == 0)
            {
                //generate the short key name, somehow pointer arithmetic works against the key string...
                const char* shortkey = xtags->key.c_str() + hstore_column->size();

                //and pack the shortkey with its value into the hstore
                //hstore ASCII representation looks like "key"=>"value"
                if(added)
                    values.push_back(',');
                escape4hstore(shortkey, values);
                values.append("=>");
                escape4hstore(xtags->value.c_str(), values);

                //we did at least one so we need commas from here on out
                added = true;
            }
        }

        //if you found not matching tags write a NUL
        if(!added)
            values.append("\\N");

        //finish the column off with a tab
        values.push_back('\t');
    }
}

//create an escaped version of the string for hstore table insert
void table_t::escape4hstore(const char *src, string& dst)
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
void table_t::escape_type(const string &value, const string &type, string& dst) {

    // For integers we take the first number, or the average if it's a-b
    if (type == "int4") {
        int from, to;
        int items = sscanf(value.c_str(), "%d-%d", &from, &to);
        if (items == 1)
            dst.append((single_fmt % from).str());
        else if (items == 2)
            dst.append((single_fmt % ((from + to) / 2)).str());
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
    else if (type == "real")
    {
        string escaped(value);
        std::replace(escaped.begin(), escaped.end(), ',', '.');

        float from, to;
        int items = sscanf(escaped.c_str(), "%f-%f", &from, &to);
        if (items == 1)
        {
            if (escaped.size() > 1 && escaped.substr(escaped.size() - 2).compare("ft") == 0)
                from *= 0.3048;
            dst.append((single_fmt % from).str());
        }
        else if (items == 2)
        {
            if (escaped.size() > 1 && escaped.substr(escaped.size() - 2).compare("ft") == 0)
            {
                from *= 0.3048;
                to *= 0.3048;
            }
            dst.append((single_fmt % ((from + to) / 2)).str());
        }
        else
            dst.append("\\N");
    }//just a string
    else
        escape(value, dst);
}

table_t::wkb_reader table_t::get_wkb_reader(const osmid_t id)
{
    //cant get wkb using the prepared statement without stopping the copy first
    stop_copy();

    char const *paramValues[1];
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    //the prepared statement get_wkb will behave differently depending on the sql_conn
    //each table has its own sql_connection with the get_way referring to the appropriate table
    PGresult* res = pgsql_execPrepared(sql_conn, "get_wkb", 1, (const char * const *)paramValues, PGRES_TUPLES_OK);
    return wkb_reader(res);
}
