#include <exception>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <utility>
#include <time.h>

#include <boost/format.hpp>

#include "options.hpp"
#include "table.hpp"
#include "taginfo.hpp"
#include "util.hpp"
#include "wkb.hpp"

using std::string;
typedef boost::format fmt;

#define BUFFER_SEND_SIZE 1024

table_t::table_t(string const &name, string const &type,
                 columns_t const &columns, hstores_t const &hstore_columns,
                 int const srid, bool const append, int const hstore_mode,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread)
: m_target(std::make_shared<db_target_descr_t>(name.c_str(), "osm_id")),
  type(type), sql_conn(nullptr), srid((fmt("%1%") % srid).str()),
  append(append), hstore_mode(hstore_mode), columns(columns),
  hstore_columns(hstore_columns), m_copy(copy_thread)
{
    //if we dont have any columns
    if(columns.size() == 0 && hstore_mode != HSTORE_ALL)
        throw std::runtime_error((fmt("No columns provided for table %1%") % name).str());

    generate_copy_column_list();
}

table_t::table_t(table_t const &other,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread)
: m_conninfo(other.m_conninfo), m_target(other.m_target), type(other.type),
  sql_conn(nullptr), srid(other.srid), append(other.append),
  hstore_mode(other.hstore_mode), columns(other.columns),
  hstore_columns(other.hstore_columns), m_table_space(other.m_table_space),
  m_copy(copy_thread)
{
    // if the other table has already started, then we want to execute
    // the same stuff to get into the same state. but if it hasn't, then
    // this would be premature.
    if (other.sql_conn) {
        connect();
        prepare();
    }
}

table_t::~table_t()
{
    teardown();
}

void table_t::teardown()
{
    if (sql_conn != nullptr) {
        PQfinish(sql_conn);
        sql_conn = nullptr;
    }
}

void table_t::commit()
{
    m_copy.sync();
}

void table_t::connect()
{
    //connect
    PGconn *_conn = PQconnectdb(m_conninfo.c_str());
    if (PQstatus(_conn) != CONNECTION_OK)
        throw std::runtime_error((fmt("Connection to database failed: %1%\n") % PQerrorMessage(_conn)).str());
    sql_conn = _conn;
    //let commits happen faster by delaying when they actually occur
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");
}

void table_t::start(std::string const &conninfo,
                    boost::optional<std::string> const &table_space)
{
    if(sql_conn)
        throw std::runtime_error(m_target->name +
                                 " cannot start, its already started");

    m_conninfo = conninfo;
    m_table_space = table_space ? " TABLESPACE " + table_space.get() : "";

    connect();
    fprintf(stderr, "Setting up table: %s\n", m_target->name.c_str());
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "SET client_min_messages = WARNING");
    //we are making a new table
    if (!append)
    {
        pgsql_exec_simple(
            sql_conn, PGRES_COMMAND_OK,
            (fmt("DROP TABLE IF EXISTS %1% CASCADE") % m_target->name).str());
    }

    /* These _tmp tables can be left behind if we run out of disk space */
    pgsql_exec_simple(
        sql_conn, PGRES_COMMAND_OK,
        (fmt("DROP TABLE IF EXISTS %1%_tmp") % m_target->name).str());

    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "RESET client_min_messages");

    //making a new table
    if (!append)
    {
        //define the new table
        string sql = (fmt("CREATE UNLOGGED TABLE %1% (osm_id %2%,") %
                      m_target->name % POSTGRES_OSMID_TYPE)
                         .str();

        //first with the regular columns
        for (auto const &column : columns) {
            sql += (fmt("\"%1%\" %2%,") % column.name % column.type_name).str();
        }

        //then with the hstore columns
        for (auto const &hcolumn : hstore_columns)
            sql += (fmt("\"%1%\" hstore,") % hcolumn).str();

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
        sql += m_table_space;

        //create the table
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, sql);
    }//appending
    else {
        //check the columns against those in the existing table
        auto res = pgsql_exec_simple(
            sql_conn, PGRES_TUPLES_OK,
            (fmt("SELECT * FROM %1% LIMIT 0") % m_target->name).str());
        for (auto const &column :  columns) {
            if (PQfnumber(res.get(), ('"' + column.name + '"').c_str()) < 0) {
#if 0
                throw std::runtime_error((fmt("Append failed. Column \"%1%\" is missing from \"%1%\"\n") % info.name).str());
#else
                fprintf(stderr, "%s",
                        (fmt("Adding new column \"%1%\" to \"%2%\"\n") %
                         column.name % m_target->name)
                            .str()
                            .c_str());
                pgsql_exec_simple(
                    sql_conn, PGRES_COMMAND_OK,
                    (fmt("ALTER TABLE %1% ADD COLUMN \"%2%\" %3%") %
                     m_target->name % column.name % column.type_name)
                        .str());
#endif
            }
            //Note: we do not verify the type or delete unused columns
        }

        //TODO: check over hstore columns

        //TODO: change the type of the geometry column if needed - this can only change to a more permissive type
    }

    prepare();
}

void table_t::prepare()
{
    //let postgres cache this query as it will presumably happen a lot
    pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK,
                      (fmt("PREPARE get_wkb (" POSTGRES_OSMID_TYPE
                           ") AS SELECT way FROM %1% WHERE osm_id = $1") %
                       m_target->name)
                          .str());
}

void table_t::generate_copy_column_list()
{
    m_target->rows = "osm_id,";
    //first with the regular columns
    for (auto const &column : columns) {
        m_target->rows += '"';
        m_target->rows += column.name;
        m_target->rows += "\",";
    }

    //then with the hstore columns
    for (auto const &hcolumn : hstore_columns) {
        m_target->rows += '"';
        m_target->rows += hcolumn;
        m_target->rows += "\",";
    }

    //add tags column and geom column
    if (hstore_mode != HSTORE_NONE)
        m_target->rows += "tags,way";
    //or just the geom column
    else
        m_target->rows += "way";
}

void table_t::stop(bool updateable, bool enable_hstore_index,
                   boost::optional<std::string> const &table_space_index)
{
    // make sure that all data is written to the DB before continuing
    m_copy.sync();

    if (!append)
    {
        time_t start, end;
        time(&start);

        fprintf(stderr, "Sorting data and creating indexes for %s\n",
                m_target->name.c_str());

        if (srid == "4326") {
            /* libosmium assures validity of geometries in 4326, so the WHERE can be skipped.
               Because we know the geom is already in 4326, no reprojection is needed for GeoHashing */
            pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK,
                              (fmt("CREATE TABLE %1%_tmp %2% AS\n"
                                   "  SELECT * FROM %1%\n"
                                   "    ORDER BY ST_GeoHash(way,10)\n"
                                   "    COLLATE \"C\"") %
                               m_target->name % m_table_space)
                                  .str());
        } else {
            /* osm2pgsql's transformation from 4326 to another projection could make a geometry invalid,
               and these need to be filtered. Also, a transformation is needed for geohashing.
               Notices are expected and ignored because they mean nothing aboud the validity of the geom
               in OSM. */
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET client_min_messages = WARNING");
            pgsql_exec_simple(
                sql_conn, PGRES_COMMAND_OK,
                (fmt("CREATE TABLE %1%_tmp %2% AS\n"
                     "  SELECT * FROM %1%\n"
                     "    WHERE ST_IsValid(way)\n"
                     // clang-format off
                     "    ORDER BY ST_GeoHash(ST_Transform(ST_Envelope(way),4326),10)\n"
                     // clang-format on
                     "    COLLATE \"C\"") %
                 m_target->name % m_table_space)
                    .str());
            pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, "RESET client_min_messages");
        }
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK,
                          (fmt("DROP TABLE %1%") % m_target->name).str());
        pgsql_exec_simple(
            sql_conn, PGRES_COMMAND_OK,
            (fmt("ALTER TABLE %1%_tmp RENAME TO %1%") % m_target->name).str());
        fprintf(stderr, "Copying %s to cluster by geometry finished\n",
                m_target->name.c_str());
        fprintf(stderr, "Creating geometry index on %s\n",
                m_target->name.c_str());

        std::string tblspc_sql =
            table_space_index ? "TABLESPACE " + table_space_index.get() : "";
        // Use fillfactor 100 for un-updatable imports
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK,
                          (fmt("CREATE INDEX ON %1% USING GIST (way) %2% %3%") %
                           m_target->name %
                           (updateable ? "" : "WITH (FILLFACTOR=100)") %
                           tblspc_sql)
                              .str());

        /* slim mode needs this to be able to apply diffs */
        if (updateable) {
            fprintf(stderr, "Creating osm_id index on %s\n",
                    m_target->name.c_str());
            pgsql_exec_simple(
                sql_conn, PGRES_COMMAND_OK,
                (fmt("CREATE INDEX ON %1% USING BTREE (osm_id) %2%") %
                 m_target->name % tblspc_sql)
                    .str());
            if (srid != "4326") {
                pgsql_exec_simple(
                    sql_conn, PGRES_COMMAND_OK,
                    (fmt("CREATE OR REPLACE FUNCTION %1%_osm2pgsql_valid()\n"
                         "RETURNS TRIGGER AS $$\n"
                         "BEGIN\n"
                         "  IF ST_IsValid(NEW.way) THEN \n"
                         "    RETURN NEW;\n"
                         "  END IF;\n"
                         "  RETURN NULL;\n"
                         "END;"
                         "$$ LANGUAGE plpgsql;") %
                     m_target->name)
                        .str());

                pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK,
                                  (fmt("CREATE TRIGGER %1%_osm2pgsql_valid "
                                       "BEFORE INSERT OR UPDATE\n"
                                       "  ON %1%\n"
                                       "    FOR EACH ROW EXECUTE PROCEDURE "
                                       "%1%_osm2pgsql_valid();") %
                                   m_target->name)
                                      .str());
            }
            //pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK, (fmt("ALTER TABLE %1% ADD CHECK (ST_IsValid(way));") % name).str());
        }
        /* Create hstore index if selected */
        if (enable_hstore_index) {
            fprintf(stderr, "Creating hstore indexes on %s\n",
                    m_target->name.c_str());
            if (hstore_mode != HSTORE_NONE) {
                pgsql_exec_simple(
                    sql_conn, PGRES_COMMAND_OK,
                    (fmt("CREATE INDEX ON %1% USING GIN (tags) %2%") %
                     m_target->name %
                     (table_space_index
                          ? "TABLESPACE " + table_space_index.get()
                          : ""))
                        .str());
            }
            for(size_t i = 0; i < hstore_columns.size(); ++i) {
                pgsql_exec_simple(
                    sql_conn, PGRES_COMMAND_OK,
                    (fmt("CREATE INDEX ON %1% USING GIN (\"%3%\") %4%") %
                     m_target->name % i % hstore_columns[i] %
                     (table_space_index
                          ? "TABLESPACE " + table_space_index.get()
                          : ""))
                        .str());
            }
        }
        fprintf(stderr, "Creating indexes on %s finished\n",
                m_target->name.c_str());
        pgsql_exec_simple(sql_conn, PGRES_COMMAND_OK,
                          (fmt("ANALYZE %1%") % m_target->name).str());
        time(&end);
        fprintf(stderr, "All indexes on %s created in %ds\n",
                m_target->name.c_str(), (int)(end - start));
    }
    teardown();

    fprintf(stderr, "Completed %s\n", m_target->name.c_str());
}

void table_t::delete_row(const osmid_t id)
{
    m_copy.new_line(m_target);
    m_copy.delete_id(id);
}

void table_t::write_row(osmid_t id, taglist_t const &tags, std::string const &geom)
{
    m_copy.new_line(m_target);

    //add the osm id
    m_copy.add_column(id);

    // used to remember which columns have been written out already.
    std::vector<bool> used;

    if (hstore_mode != HSTORE_NONE)
        used.assign(tags.size(), false);

    //get the regular columns' values
    write_columns(tags, hstore_mode == HSTORE_NORM ? &used : nullptr);

    //get the hstore columns' values
    write_hstore_columns(tags);

    //get the key value pairs for the tags column
    if (hstore_mode != HSTORE_NONE)
        write_tags_column(tags, used);

    //add the geometry - encoding it to hex along the way
    m_copy.add_hex_geom(geom);

    //send all the data to postgres
    m_copy.finish_line();
}

void table_t::write_columns(taglist_t const &tags, std::vector<bool> *used)
{
    //for each column
    for (auto const &column : columns) {
        int idx;
        if ((idx = tags.indexof(column.name)) >= 0) {
            escape_type(tags[(size_t)idx].value, column.type);

            // Remember we already used this one so we can't use
            // again later in the hstore column.
            if (used)
                (*used)[(size_t)idx] = true;
        } else {
            m_copy.add_null_column();
        }
    }
}

void table_t::write_tags_column(taglist_t const &tags,
                                std::vector<bool> const &used)
{
    m_copy.new_hash();
    //iterate through the list of tags, first one is always null
    for (size_t i = 0; i < tags.size(); ++i)
    {
        const tag_t& xtag = tags[i];
        //skip z_order tag and keys which have their own column
        if (used[i] || ("z_order" == xtag.key))
            continue;

        m_copy.add_hash_elem(xtag.key, xtag.value);
    }
    m_copy.finish_hash();
}

/* write an hstore column to the database */
void table_t::write_hstore_columns(const taglist_t &tags)
{
    //iterate over all configured hstore columns in the options
    for (auto const &hstore_column : hstore_columns) {
        bool added = false;

        //iterate through the list of tags, first one is always null
        for (auto const &xtags : tags) {
            //check if the tag's key starts with the name of the hstore column
            if (xtags.key.compare(0, hstore_column.size(), hstore_column) ==
                0) {
                //generate the short key name, somehow pointer arithmetic works against the key string...
                char const *shortkey = xtags.key.c_str() + hstore_column.size();

                //and pack the shortkey with its value into the hstore
                //hstore ASCII representation looks like "key"=>"value"
                if (!added) {
                    added = true;
                    m_copy.new_hash();
                }

                m_copy.add_hash_elem(shortkey, xtags.value.c_str());
            }
        }

        if (added) {
            m_copy.finish_hash();
        } else {
            m_copy.add_null_column();
        }
    }
}

/* Escape data appropriate to the type */
void table_t::escape_type(const string &value, ColumnType flags)
{
    switch (flags) {
    case COLUMN_TYPE_INT: {
        // For integers we take the first number, or the average if it's a-b
        long from, to;
        int items = sscanf(value.c_str(), "%ld-%ld", &from, &to);
        if (items == 1) {
            m_copy.add_column(from);
        } else if (items == 2) {
            m_copy.add_column((from + to) / 2);
        } else {
            m_copy.add_null_column();
        }
        break;
    }
    case COLUMN_TYPE_REAL:
        /* try to "repair" real values as follows:
             * assume "," to be a decimal mark which need to be replaced by "."
             * like int4 take the first number, or the average if it's a-b
             * assume SI unit (meters)
             * convert feet to meters (1 foot = 0.3048 meters)
             * reject anything else
             */
        {
            string escaped(value);
            std::replace(escaped.begin(), escaped.end(), ',', '.');

            double from, to;
            int items = sscanf(escaped.c_str(), "%lf-%lf", &from, &to);
            if (items == 1) {
                if (escaped.size() > 1 &&
                    escaped.substr(escaped.size() - 2).compare("ft") == 0) {
                    from *= 0.3048;
                }
                m_copy.add_column(from);
            } else if (items == 2) {
                if (escaped.size() > 1 &&
                    escaped.substr(escaped.size() - 2).compare("ft") == 0) {
                    from *= 0.3048;
                    to *= 0.3048;
                }
                m_copy.add_column((from + to) / 2);
            } else {
                m_copy.add_null_column();
            }
            break;
        }
    case COLUMN_TYPE_TEXT:
        //just a string
        m_copy.add_column(value);
        break;
    }
}

table_t::wkb_reader table_t::get_wkb_reader(const osmid_t id)
{
    char const *paramValues[1];
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    //the prepared statement get_wkb will behave differently depending on the sql_conn
    //each table has its own sql_connection with the get_way referring to the appropriate table
    auto res =
        pgsql_execPrepared(sql_conn, "get_wkb", 1,
                           (const char *const *)paramValues, PGRES_TUPLES_OK);
    return wkb_reader(std::move(res));
}
