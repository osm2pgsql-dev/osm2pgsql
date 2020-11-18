#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "format.hpp"
#include "logging.hpp"
#include "options.hpp"
#include "pgsql-helper.hpp"
#include "table.hpp"
#include "taginfo.hpp"
#include "util.hpp"
#include "wkb.hpp"

table_t::table_t(std::string const &name, std::string const &type,
                 columns_t const &columns, hstores_t const &hstore_columns,
                 int const srid, bool const append, hstore_column hstore_mode,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread,
                 std::string const &schema)
: m_target(std::make_shared<db_target_descr_t>(name.c_str(), "osm_id")),
  m_type(type), m_srid(fmt::to_string(srid)), m_append(append),
  m_hstore_mode(hstore_mode), m_columns(columns),
  m_hstore_columns(hstore_columns), m_copy(copy_thread)
{
    m_target->schema = schema;

    // if we dont have any columns
    if (m_columns.empty() && m_hstore_mode != hstore_column::all) {
        throw std::runtime_error{
            "No columns provided for table {}."_format(name)};
    }

    generate_copy_column_list();
}

table_t::table_t(table_t const &other,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread)
: m_conninfo(other.m_conninfo), m_target(other.m_target), m_type(other.m_type),
  m_srid(other.m_srid), m_append(other.m_append),
  m_hstore_mode(other.m_hstore_mode), m_columns(other.m_columns),
  m_hstore_columns(other.m_hstore_columns), m_table_space(other.m_table_space),
  m_copy(copy_thread)
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

void table_t::sync() { m_copy.sync(); }

void table_t::connect()
{
    m_sql_conn.reset(new pg_conn_t{m_conninfo});
    //let commits happen faster by delaying when they actually occur
    m_sql_conn->exec("SET synchronous_commit = off");
}

void table_t::start(std::string const &conninfo, std::string const &table_space)
{
    if (m_sql_conn) {
        throw std::runtime_error{m_target->name +
                                 " cannot start, its already started."};
    }

    m_conninfo = conninfo;
    m_table_space = tablespace_clause(table_space);

    connect();
    log_info("Setting up table: {}", m_target->name);
    m_sql_conn->exec("SET client_min_messages = WARNING");
    auto const qual_name = qualified_name(m_target->schema, m_target->name);
    auto const qual_tmp_name = qualified_name(
        m_target->schema, m_target->name + "_tmp");

    // we are making a new table
    if (!m_append) {
        m_sql_conn->exec(
            "DROP TABLE IF EXISTS {} CASCADE"_format(qual_name));
    }

    // These _tmp tables can be left behind if we run out of disk space.
    m_sql_conn->exec("DROP TABLE IF EXISTS {}"_format(qual_tmp_name));
    m_sql_conn->exec("RESET client_min_messages");

    //making a new table
    if (!m_append) {
        //define the new table
        auto sql =
            "CREATE UNLOGGED TABLE {} (osm_id int8,"_format(qual_name);

        //first with the regular columns
        for (auto const &column : m_columns) {
            sql += "\"{}\" {},"_format(column.name, column.type_name);
        }

        //then with the hstore columns
        for (auto const &hcolumn : m_hstore_columns) {
            sql += "\"{}\" hstore,"_format(hcolumn);
        }

        //add tags column
        if (m_hstore_mode != hstore_column::none) {
            sql += "\"tags\" hstore,";
        }

        sql += "way geometry({},{}) )"_format(m_type, m_srid);

        // The final tables are created with CREATE TABLE AS ... SELECT * FROM ...
        // This means that they won't get this autovacuum setting, so it doesn't
        // doesn't need to be RESET on these tables
        sql += " WITH (autovacuum_enabled = off)";
        //add the main table space
        sql += m_table_space;

        //create the table
        m_sql_conn->exec(sql);
    } //appending
    else {
        //check the columns against those in the existing table
        auto const res = m_sql_conn->query(
            PGRES_TUPLES_OK, "SELECT * FROM {} LIMIT 0"_format(qual_name));
        for (auto const &column : m_columns) {
            if (res.get_column_number(column.name) < 0) {
                log_info("Adding new column \"{}\" to \"{}\"", column.name,
                         m_target->name);
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
    auto const qual_name = qualified_name(m_target->schema, m_target->name);
    m_sql_conn->exec(
        "PREPARE get_wkb(int8) AS SELECT way FROM {} WHERE osm_id = $1"_format(
            qual_name));
}

void table_t::generate_copy_column_list()
{
    m_target->rows = "osm_id,";
    //first with the regular columns
    for (auto const &column : m_columns) {
        m_target->rows += '"';
        m_target->rows += column.name;
        m_target->rows += "\",";
    }

    //then with the hstore columns
    for (auto const &hcolumn : m_hstore_columns) {
        m_target->rows += '"';
        m_target->rows += hcolumn;
        m_target->rows += "\",";
    }

    //add tags column and geom column
    if (m_hstore_mode != hstore_column::none) {
        m_target->rows += "tags,way";
        //or just the geom column
    } else {
        m_target->rows += "way";
    }
}

void table_t::stop(bool updateable, bool enable_hstore_index,
                   std::string const &table_space_index)
{
    // make sure that all data is written to the DB before continuing
    m_copy.sync();

    auto const qual_name = qualified_name(m_target->schema, m_target->name);
    auto const qual_tmp_name = qualified_name(
        m_target->schema, m_target->name + "_tmp");

    if (!m_append) {
        util::timer_t timer;

        log_info("Sorting data and creating indexes for {}", m_target->name);

        // Notices about invalid geometries are expected and can be ignored
        // because they say nothing about the validity of the geometry in OSM.
        m_sql_conn->exec("SET client_min_messages = WARNING");

        std::string sql =
            "CREATE TABLE {} {} AS SELECT * FROM {}"_format(
                qual_tmp_name, m_table_space, qual_name);

        if (m_srid != "4326") {
            // libosmium assures validity of geometries in 4326.
            // Transformation to another projection could make the geometry
            // invalid. Therefore add a filter to drop those.
            sql += " WHERE ST_IsValid(way)";
        }

        auto const postgis_version = get_postgis_version(*m_sql_conn);

        sql += " ORDER BY ";
        if (postgis_version.major == 2 && postgis_version.minor < 4) {
            log_info("Using GeoHash for clustering");
            if (m_srid == "4326") {
                sql += "ST_GeoHash(way,10)";
            } else {
                sql += "ST_GeoHash(ST_Transform(ST_Envelope(way),4326),10)";
            }
            sql += " COLLATE \"C\"";
        } else {
            log_info("Using native order for clustering");
            // Since Postgis 2.4 the order function for geometries gives
            // useful results.
            sql += "way";
        }

        m_sql_conn->exec(sql);

        m_sql_conn->exec("DROP TABLE {}"_format(qual_name));
        m_sql_conn->exec(
            "ALTER TABLE {} RENAME TO {}"_format(qual_tmp_name, m_target->name));
        log_info("Copying {} to cluster by geometry finished", m_target->name);
        log_info("Creating geometry index on {}", m_target->name);

        // Use fillfactor 100 for un-updatable imports
        m_sql_conn->exec("CREATE INDEX ON {} USING GIST (way) {} {}"_format(
            qual_name, (updateable ? "" : "WITH (fillfactor = 100)"),
            tablespace_clause(table_space_index)));

        /* slim mode needs this to be able to apply diffs */
        if (updateable) {
            log_info("Creating osm_id index on {}", m_target->name);
            m_sql_conn->exec(
                "CREATE INDEX ON {} USING BTREE (osm_id) {}"_format(
                    qual_name, tablespace_clause(table_space_index)));
            if (m_srid != "4326") {
                create_geom_check_trigger(m_sql_conn.get(), m_target->schema,
                                          m_target->name, "way");
            }
        }
        /* Create hstore index if selected */
        if (enable_hstore_index) {
            log_info("Creating hstore indexes on {}", m_target->name);
            if (m_hstore_mode != hstore_column::none) {
                m_sql_conn->exec(
                    "CREATE INDEX ON {} USING GIN (tags) {}"_format(
                        qual_name, tablespace_clause(table_space_index)));
            }
            for (auto const &hcolumn : m_hstore_columns) {
                m_sql_conn->exec(
                    "CREATE INDEX ON {} USING GIN (\"{}\") {}"_format(
                        qual_name, hcolumn,
                        tablespace_clause(table_space_index)));
            }
        }
        log_info("Creating indexes on {} finished", m_target->name);
        m_sql_conn->exec("ANALYZE {}"_format(qual_name));
        log_info("All indexes on {} created in {}", m_target->name,
                 util::human_readable_duration(timer.stop()));
    }
    teardown();

    log_info("Completed table {}", m_target->name);
}

void table_t::delete_row(osmid_t const id)
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

    if (m_hstore_mode != hstore_column::none) {
        used.assign(tags.size(), false);
    }

    //get the regular columns' values
    write_columns(tags, m_hstore_mode == hstore_column::norm ? &used : nullptr);

    //get the hstore columns' values
    write_hstore_columns(tags);

    //get the key value pairs for the tags column
    if (m_hstore_mode != hstore_column::none) {
        write_tags_column(tags, used);
    }

    //add the geometry - encoding it to hex along the way
    m_copy.add_hex_geom(geom);

    //send all the data to postgres
    m_copy.finish_line();
}

void table_t::write_columns(taglist_t const &tags, std::vector<bool> *used)
{
    for (auto const &column : m_columns) {
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
void table_t::write_hstore_columns(taglist_t const &tags)
{
    for (auto const &hcolumn : m_hstore_columns) {
        bool added = false;

        for (auto const &tag : tags) {
            //check if the tag's key starts with the name of the hstore column
            if (tag.key.compare(0, hcolumn.size(), hcolumn) == 0) {
                char const *const shortkey = &tag.key[hcolumn.size()];

                //and pack the shortkey with its value into the hstore
                //hstore ASCII representation looks like "key"=>"value"
                if (!added) {
                    added = true;
                    m_copy.new_hash();
                }

                m_copy.add_hash_elem(shortkey, tag.value.c_str());
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
void table_t::escape_type(std::string const &value, ColumnType flags)
{
    switch (flags) {
    case ColumnType::INT: {
        // For integers we take the first number, or the average if it's a-b
        long long from, to;
        // limit number of digits parsed to avoid undefined behaviour in sscanf
        int const items =
            std::sscanf(value.c_str(), "%18lld-%18lld", &from, &to);
        if (items == 1 && from <= std::numeric_limits<int32_t>::max() &&
            from >= std::numeric_limits<int32_t>::min()) {
            m_copy.add_column(from);
        } else if (items == 2) {
            // calculate mean while avoiding overflows
            int64_t const mean =
                (from / 2) + (to / 2) + ((from % 2 + to % 2) / 2);
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
    case ColumnType::REAL:
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
            int const items =
                std::sscanf(escaped.c_str(), "%lf-%lf", &from, &to);
            if (items == 1) {
                if (escaped.size() > 1 &&
                    escaped.substr(escaped.size() - 2) == "ft") {
                    from *= 0.3048;
                }
                m_copy.add_column(from);
            } else if (items == 2) {
                if (escaped.size() > 1 &&
                    escaped.substr(escaped.size() - 2) == "ft") {
                    from *= 0.3048;
                    to *= 0.3048;
                }
                m_copy.add_column((from + to) / 2);
            } else {
                m_copy.add_null_column();
            }
            break;
        }
    case ColumnType::TEXT:
        m_copy.add_column(value);
        break;
    }
}

table_t::wkb_reader table_t::get_wkb_reader(osmid_t id)
{
    auto res = m_sql_conn->exec_prepared("get_wkb", id);
    return wkb_reader{std::move(res)};
}
