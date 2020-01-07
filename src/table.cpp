#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <utility>

#include "format.hpp"
#include "options.hpp"
#include "table.hpp"
#include "taginfo.hpp"
#include "util.hpp"
#include "wkb.hpp"

#define BUFFER_SEND_SIZE 1024

table_t::table_t(std::string const &name, std::string const &type,
                 columns_t const &columns, hstores_t const &hstore_columns,
                 int const srid, bool const append, int const hstore_mode,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread)
: m_target(std::make_shared<db_target_descr_t>(name.c_str(), "osm_id")),
  type(type), srid(fmt::to_string(srid)), append(append),
  hstore_mode(hstore_mode), columns(columns), hstore_columns(hstore_columns),
  m_copy(copy_thread)
{
    // if we dont have any columns
    if (columns.size() == 0 && hstore_mode != HSTORE_ALL) {
        throw std::runtime_error{
            "No columns provided for table {}"_format(name)};
    }

    generate_copy_column_list();
}

table_t::table_t(table_t const &other,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread)
: m_conninfo(other.m_conninfo), m_target(other.m_target), type(other.type),
  srid(other.srid), append(other.append), hstore_mode(other.hstore_mode),
  columns(other.columns), hstore_columns(other.hstore_columns),
  m_table_space(other.m_table_space), m_copy(copy_thread)
{
    // if the other table has already started, then we want to execute
    // the same stuff to get into the same state. but if it hasn't, then
    // this would be premature.
    if (other.m_sql_conn) {
        connect();
        prepare();
    }
}

void table_t::teardown() { m_sql_conn.reset(); }

void table_t::commit() { m_copy.sync(); }

void table_t::connect()
{
    //connect
    m_sql_conn.reset(new pg_conn_t{m_conninfo});
    //let commits happen faster by delaying when they actually occur
    m_sql_conn->exec("SET synchronous_commit TO off");
}

void table_t::start(std::string const &conninfo,
                    boost::optional<std::string> const &table_space)
{
    if (m_sql_conn) {
        throw std::runtime_error(m_target->name +
                                 " cannot start, its already started");
    }

    m_conninfo = conninfo;
    m_table_space = table_space ? " TABLESPACE " + table_space.get() : "";

    connect();
    fprintf(stderr, "Setting up table: %s\n", m_target->name.c_str());
    m_sql_conn->exec("SET client_min_messages = WARNING");
    // we are making a new table
    if (!append) {
        m_sql_conn->exec(
            "DROP TABLE IF EXISTS {} CASCADE"_format(m_target->name));
    }

    // These _tmp tables can be left behind if we run out of disk space.
    m_sql_conn->exec("DROP TABLE IF EXISTS {}_tmp"_format(m_target->name));
    m_sql_conn->exec("RESET client_min_messages");

    //making a new table
    if (!append) {
        //define the new table
        auto sql = "CREATE UNLOGGED TABLE {} (osm_id {},"_format(
            m_target->name, POSTGRES_OSMID_TYPE);

        //first with the regular columns
        for (auto const &column : columns) {
            sql += "\"{}\" {},"_format(column.name, column.type_name);
        }

        //then with the hstore columns
        for (auto const &hcolumn : hstore_columns) {
            sql += "\"{}\" hstore,"_format(hcolumn);
        }

        //add tags column
        if (hstore_mode != HSTORE_NONE) {
            sql += "\"tags\" hstore,";
        }

        sql += "way geometry({},{}) )"_format(type, srid);

        // The final tables are created with CREATE TABLE AS ... SELECT * FROM ...
        // This means that they won't get this autovacuum setting, so it doesn't
        // doesn't need to be RESET on these tables
        sql += " WITH ( autovacuum_enabled = FALSE )";
        //add the main table space
        sql += m_table_space;

        //create the table
        m_sql_conn->exec(sql);
    } //appending
    else {
        //check the columns against those in the existing table
        auto const res = m_sql_conn->query(
            PGRES_TUPLES_OK, "SELECT * FROM {} LIMIT 0"_format(m_target->name));
        for (auto const &column : columns) {
            if (res.get_column_number(column.name) < 0) {
                fmt::print(stderr, "Adding new column \"{}\" to \"{}\"\n",
                           column.name, m_target->name);
                m_sql_conn->exec("ALTER TABLE {} ADD COLUMN \"{}\" {}"_format(
                    m_target->name, column.name, column.type_name));
            }
            // Note: we do not verify the type or delete unused columns
        }

        //TODO: check over hstore columns

        //TODO: change the type of the geometry column if needed - this can only change to a more permissive type
    }

    prepare();
}

void table_t::prepare()
{
    //let postgres cache this query as it will presumably happen a lot
    m_sql_conn->exec(
        "PREPARE get_wkb (" POSTGRES_OSMID_TYPE
        ") AS SELECT way FROM {} WHERE osm_id = $1"_format(m_target->name));
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
    if (hstore_mode != HSTORE_NONE) {
        m_target->rows += "tags,way";
        //or just the geom column
    } else {
        m_target->rows += "way";
    }
}

void table_t::stop(bool updateable, bool enable_hstore_index,
                   boost::optional<std::string> const &table_space_index)
{
    // make sure that all data is written to the DB before continuing
    m_copy.sync();

    if (!append) {
        time_t start, end;
        time(&start);

        fprintf(stderr, "Sorting data and creating indexes for %s\n",
                m_target->name.c_str());

        // Notices about invalid geometries are expected and can be ignored
        // because they say nothing about the validity of the geometry in OSM.
        m_sql_conn->exec("SET client_min_messages = WARNING");

        std::string sql =
            "CREATE TABLE {0}_tmp {1} AS SELECT * FROM {0}"_format(
                m_target->name, m_table_space);

        if (srid != "4326") {
            // libosmium assures validity of geometries in 4326.
            // Transformation to another projection could make the geometry
            // invalid. Therefore add a filter to drop those.
            sql += " WHERE ST_IsValid(way)";
        }

        auto res = m_sql_conn->query(
            PGRES_TUPLES_OK,
            "SELECT regexp_split_to_table(postgis_lib_version(), '\\.')");
        auto const postgis_major = std::stoi(res.get_value_as_string(0, 0));
        auto const postgis_minor = std::stoi(res.get_value_as_string(1, 0));

        sql += " ORDER BY ";
        if (postgis_major == 2 && postgis_minor < 4) {
            fprintf(stderr, "Using GeoHash for clustering\n");
            if (srid == "4326") {
                sql += "ST_GeoHash(way,10)";
            } else {
                sql += "ST_GeoHash(ST_Transform(ST_Envelope(way),4326),10)";
            }
            sql += " COLLATE \"C\"";
        } else {
            fprintf(stderr, "Using native order for clustering\n");
            // Since Postgis 2.4 the order function for geometries gives
            // useful results.
            sql += "way";
        }

        m_sql_conn->exec(sql);

        m_sql_conn->exec("DROP TABLE {}"_format(m_target->name));
        m_sql_conn->exec(
            "ALTER TABLE {0}_tmp RENAME TO {0}"_format(m_target->name));
        fprintf(stderr, "Copying %s to cluster by geometry finished\n",
                m_target->name.c_str());
        fprintf(stderr, "Creating geometry index on %s\n",
                m_target->name.c_str());

        std::string tblspc_sql =
            table_space_index ? "TABLESPACE " + table_space_index.get() : "";
        // Use fillfactor 100 for un-updatable imports
        m_sql_conn->exec("CREATE INDEX ON {} USING GIST (way) {} {}"_format(
            m_target->name, (updateable ? "" : "WITH (FILLFACTOR=100)"),
            tblspc_sql));

        /* slim mode needs this to be able to apply diffs */
        if (updateable) {
            fprintf(stderr, "Creating osm_id index on %s\n",
                    m_target->name.c_str());
            m_sql_conn->exec(
                "CREATE INDEX ON {} USING BTREE (osm_id) {}"_format(
                    m_target->name, tblspc_sql));
            if (srid != "4326") {
                m_sql_conn->exec(
                    "CREATE OR REPLACE FUNCTION {}_osm2pgsql_valid()\n"
                    "RETURNS TRIGGER AS $$\n"
                    "BEGIN\n"
                    "  IF ST_IsValid(NEW.way) THEN \n"
                    "    RETURN NEW;\n"
                    "  END IF;\n"
                    "  RETURN NULL;\n"
                    "END;"
                    "$$ LANGUAGE plpgsql;"_format(m_target->name));

                m_sql_conn->exec(
                    "CREATE TRIGGER {0}_osm2pgsql_valid "
                    "BEFORE INSERT OR UPDATE\n"
                    "  ON {0}\n"
                    "    FOR EACH ROW EXECUTE PROCEDURE "
                    "{0}_osm2pgsql_valid();"_format(m_target->name));
            }
        }
        /* Create hstore index if selected */
        if (enable_hstore_index) {
            fprintf(stderr, "Creating hstore indexes on %s\n",
                    m_target->name.c_str());
            if (hstore_mode != HSTORE_NONE) {
                m_sql_conn->exec(
                    "CREATE INDEX ON {} USING GIN (tags) {}"_format(
                        m_target->name,
                        (table_space_index
                             ? "TABLESPACE " + table_space_index.get()
                             : "")));
            }
            for (size_t i = 0; i < hstore_columns.size(); ++i) {
                m_sql_conn->exec(
                    "CREATE INDEX ON {} USING GIN (\"{}\") {}"_format(
                        m_target->name, hstore_columns[i],
                        (table_space_index
                             ? "TABLESPACE " + table_space_index.get()
                             : "")));
            }
        }
        fprintf(stderr, "Creating indexes on %s finished\n",
                m_target->name.c_str());
        m_sql_conn->exec("ANALYZE {}"_format(m_target->name));
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
    m_copy.delete_object(id);
}

void table_t::write_row(osmid_t id, taglist_t const &tags,
                        std::string const &geom)
{
    m_copy.new_line(m_target);

    //add the osm id
    m_copy.add_column(id);

    // used to remember which columns have been written out already.
    std::vector<bool> used;

    if (hstore_mode != HSTORE_NONE) {
        used.assign(tags.size(), false);
    }

    //get the regular columns' values
    write_columns(tags, hstore_mode == HSTORE_NORM ? &used : nullptr);

    //get the hstore columns' values
    write_hstore_columns(tags);

    //get the key value pairs for the tags column
    if (hstore_mode != HSTORE_NONE) {
        write_tags_column(tags, used);
    }

    //add the geometry - encoding it to hex along the way
    m_copy.add_hex_geom(geom);

    //send all the data to postgres
    m_copy.finish_line();
}

void table_t::write_columns(taglist_t const &tags, std::vector<bool> *used)
{
    //for each column
    for (auto const &column : columns) {
        std::size_t const idx = tags.indexof(column.name);
        if (idx != std::numeric_limits<std::size_t>::max()) {
            escape_type(tags[idx].value, column.type);

            // Remember we already used this one so we can't use
            // again later in the hstore column.
            if (used) {
                (*used)[idx] = true;
            }
        } else {
            m_copy.add_null_column();
        }
    }
}

/// Write all tags to hstore. Exclude tags written to other columns and z_order.
void table_t::write_tags_column(taglist_t const &tags,
                                std::vector<bool> const &used)
{
    m_copy.new_hash();

    for (std::size_t i = 0; i < tags.size(); ++i) {
        tag_t const &tag = tags[i];
        if (!used[i] && (tag.key != "z_order")) {
            m_copy.add_hash_elem(tag.key, tag.value);
        }
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
void table_t::escape_type(const std::string &value, ColumnType flags)
{
    switch (flags) {
    case COLUMN_TYPE_INT: {
        // For integers we take the first number, or the average if it's a-b
        long long from, to;
        // limit number of digits parsed to avoid undefined behaviour in sscanf
        int items = sscanf(value.c_str(), "%18lld-%18lld", &from, &to);
        if (items == 1 && from <= std::numeric_limits<int32_t>::max() &&
            from >= std::numeric_limits<int32_t>::min()) {
            m_copy.add_column(from);
        } else if (items == 2) {
            // calculate mean while avoiding overflows
            int64_t mean = (from / 2) + (to / 2) + ((from % 2 + to % 2) / 2);
            if (mean <= std::numeric_limits<int32_t>::max() &&
                mean >= std::numeric_limits<int32_t>::min()) {
                m_copy.add_column(mean);
            } else {
                m_copy.add_null_column();
            }
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
            std::string escaped{value};
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
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    char const *param_values[] = {tmp};

    // the prepared statement get_wkb will behave differently depending on the
    // sql_conn
    // each table has its own sql_connection with the get_way referring to the
    // appropriate table
    auto res = m_sql_conn->exec_prepared("get_wkb", 1, param_values);
    return wkb_reader{std::move(res)};
}
