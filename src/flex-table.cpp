#include "flex-table.hpp"
#include "format.hpp"
#include "util.hpp"

#include <cassert>
#include <stdexcept>
#include <string>

char const *type_to_char(osmium::item_type type) noexcept
{
    switch (type) {
    case osmium::item_type::node:
        return "N";
    case osmium::item_type::way:
        return "W";
    case osmium::item_type::relation:
        return "R";
    default:
        break;
    }
    return "X";
}

bool flex_table_t::has_multicolumn_id_index() const noexcept
{
    return m_columns[0].type() == table_column_type::id_type;
}

std::string flex_table_t::id_column_names() const
{
    std::string name;

    if (!has_id_column()) {
        return name;
    }

    name = m_columns[0].name();
    if (has_multicolumn_id_index()) {
        name += ',';
        name += m_columns[1].name();
    }

    return name;
}

std::string flex_table_t::full_name() const
{
    return "\"" + schema() + "\".\"" + name() + "\"";
}

std::string flex_table_t::full_tmp_name() const
{
    return "\"" + schema() + "\".\"" + name() + "_tmp\"";
}

flex_table_column_t &flex_table_t::add_column(std::string const &name,
                                              std::string const &type)
{
    // id_type (optional) and id_num must always be the first columns
    assert(type != "id_type" || m_columns.empty());
    assert(type != "id_num" || m_columns.empty() ||
           (m_columns.size() == 1 &&
            m_columns[0].type() == table_column_type::id_type));

    m_columns.emplace_back(name, type);
    auto &column = m_columns.back();

    if (column.is_geometry_column()) {
        m_geom_column = m_columns.size() - 1;
        column.set_not_null();
    }

    return column;
}

std::string flex_table_t::build_sql_prepare_get_wkb() const
{
    if (has_geom_column()) {
        if (has_multicolumn_id_index()) {
            return "PREPARE get_wkb(text, bigint) AS"
                   " SELECT \"{}\" FROM {} WHERE \"{}\" = '$1' AND \"{}\" = $2"_format(
                       geom_column().name(), full_name(), m_columns[0].name(),
                       m_columns[1].name());
        }
        return "PREPARE get_wkb(bigint) AS"
               " SELECT \"{}\" FROM {} WHERE \"{}\" = $1"_format(
                   geom_column().name(), full_name(), id_column_names());
    }
    return "PREPARE get_wkb(bigint) AS SELECT ''";
}

std::string flex_table_t::build_sql_create_table(bool final_table) const
{
    assert(!m_columns.empty());

    std::string sql = "CREATE {} TABLE IF NOT EXISTS {} ("_format(
        final_table ? "" : "UNLOGGED",
        final_table ? full_tmp_name() : full_name());

    for (auto const &column : m_columns) {
        sql += column.sql_create(m_srid);
    }

    assert(sql.back() == ',');
    sql.back() = ')';

    if (!final_table) {
        sql += " WITH (autovacuum_enabled = off)";
    }

    sql += tablespace_clause(m_data_tablespace);

    return sql;
}

std::string flex_table_t::build_sql_column_list() const
{
    assert(!m_columns.empty());

    std::string result;
    for (auto const &column : m_columns) {
        if (!column.create_only()) {
            result += '"';
            result += column.name();
            result += '"';
            result += ',';
        }
    }
    result.resize(result.size() - 1);

    return result;
}

std::string flex_table_t::build_sql_create_id_index() const
{
    return "CREATE INDEX ON {} USING BTREE ({}) {}"_format(
        full_name(), id_column_names(), tablespace_clause(index_tablespace()));
}

void table_connection_t::connect(std::string const &conninfo)
{
    assert(!m_db_connection);

    m_db_connection.reset(new pg_conn_t{conninfo});
    m_db_connection->exec("SET synchronous_commit = off");
}

void table_connection_t::start(bool append)
{
    assert(m_db_connection);

    m_db_connection->exec("SET client_min_messages = WARNING");

    if (!append) {
        m_db_connection->exec(
            "DROP TABLE IF EXISTS {} CASCADE"_format(table().full_name()));
    }

    // These _tmp tables can be left behind if we run out of disk space.
    m_db_connection->exec(
        "DROP TABLE IF EXISTS {}"_format(table().full_tmp_name()));
    m_db_connection->exec("RESET client_min_messages");

    if (!append) {
        if (table().schema() != "public") {
            m_db_connection->exec(
                "CREATE SCHEMA IF NOT EXISTS \"{}\""_format(table().schema()));
        }
        m_db_connection->exec(table().build_sql_create_table(false));
    } else {
        //check the columns against those in the existing table
        auto const res = m_db_connection->query(
            PGRES_TUPLES_OK,
            "SELECT * FROM {} LIMIT 0"_format(table().full_name()));

        for (auto const &column : table()) {
            if (res.get_column_number(column.name()) < 0) {
                fmt::print(stderr, "Adding new column '{}' to '{}'\n",
                           column.name(), table().name());
                m_db_connection->exec(
                    "ALTER TABLE {} ADD COLUMN \"{}\" {}"_format(
                        table().full_name(), column.name(),
                        column.sql_type_name(table().srid())));
            }
            // Note: we do not verify the type or delete unused columns
        }

        //TODO: change the type of the geometry column if needed - this can only change to a more permissive type
    }

    prepare();
}

void table_connection_t::stop(bool updateable, bool append)
{
    assert(m_db_connection);

    m_copy_mgr.sync();

    if (append) {
        teardown();
        return;
    }

    util::timer_t timer;

    if (table().has_geom_column()) {
        fmt::print(stderr, "Clustering table '{}' by geometry...\n",
                   table().name());

        // Notices about invalid geometries are expected and can be ignored
        // because they say nothing about the validity of the geometry in OSM.
        m_db_connection->exec("SET client_min_messages = WARNING");

        m_db_connection->exec(table().build_sql_create_table(true));

        std::string sql = "INSERT INTO {} SELECT * FROM {}"_format(
            table().full_tmp_name(), table().full_name());

        if (table().srid() != 4326) {
            // libosmium assures validity of geometries in 4326.
            // Transformation to another projection could make the geometry
            // invalid. Therefore add a filter to drop those.
            sql += " WHERE ST_IsValid(\"{}\")"_format(
                table().geom_column().name());
        }

        auto const res = m_db_connection->query(
            PGRES_TUPLES_OK,
            "SELECT regexp_split_to_table(postgis_lib_version(), '\\.')");
        auto const postgis_major = std::stoi(res.get_value_as_string(0, 0));
        auto const postgis_minor = std::stoi(res.get_value_as_string(1, 0));

        sql += " ORDER BY ";
        if (postgis_major == 2 && postgis_minor < 4) {
            fmt::print(stderr, "Using GeoHash for clustering\n");
            if (table().srid() == 4326) {
                sql += "ST_GeoHash({},10)"_format(table().geom_column().name());
            } else {
                sql +=
                    "ST_GeoHash(ST_Transform(ST_Envelope({}),4326),10)"_format(
                        table().geom_column().name());
            }
            sql += " COLLATE \"C\"";
        } else {
            fmt::print(stderr, "Using native order for clustering\n");
            // Since Postgis 2.4 the order function for geometries gives
            // useful results.
            sql += table().geom_column().name();
        }

        m_db_connection->exec(sql);

        m_db_connection->exec("DROP TABLE {}"_format(table().full_name()));
        m_db_connection->exec("ALTER TABLE {} RENAME TO \"{}\""_format(
            table().full_tmp_name(), table().name()));

        fmt::print(stderr, "Creating geometry index on table '{}'...\n",
                   table().name());

        // Use fillfactor 100 for un-updateable imports
        m_db_connection->exec(
            "CREATE INDEX ON {} USING GIST (\"{}\") {} {}"_format(
                table().full_name(), table().geom_column().name(),
                (updateable ? "" : "WITH (fillfactor = 100)"),
                tablespace_clause(table().index_tablespace())));
    }

    if (updateable && table().has_id_column()) {
        fmt::print(stderr, "Creating id index on table '{}'...\n",
                   table().name());
        m_db_connection->exec(table().build_sql_create_id_index());

        if (table().srid() != 4326 && table().has_geom_column()) {
            m_db_connection->exec(
                "CREATE OR REPLACE FUNCTION {}_osm2pgsql_valid()\n"
                "RETURNS TRIGGER AS $$\n"
                "BEGIN\n"
                "  IF ST_IsValid(NEW.{}) THEN \n"
                "    RETURN NEW;\n"
                "  END IF;\n"
                "  RETURN NULL;\n"
                "END;"
                "$$ LANGUAGE plpgsql;"_format(table().name(),
                                              table().geom_column().name()));

            m_db_connection->exec("CREATE TRIGGER \"{0}_osm2pgsql_valid\""
                                  " BEFORE INSERT OR UPDATE"
                                  " ON {1}"
                                  " FOR EACH ROW EXECUTE PROCEDURE"
                                  " {0}_osm2pgsql_valid();"_format(
                                      table().name(), table().full_name()));
        }
    }

    fmt::print(stderr, "Analyzing table '{}'...\n", table().name());
    m_db_connection->exec("ANALYZE \"{}\""_format(table().name()));

    fmt::print(stderr, "All postprocessing on table '{}' done in {}s.\n",
               table().name(), timer.stop());

    teardown();
}

pg_result_t table_connection_t::get_geom_by_id(osmium::item_type type,
                                               osmid_t id) const
{
    assert(table().has_geom_column());
    assert(m_db_connection);
    std::string const id_str = fmt::to_string(id);
    if (table().has_multicolumn_id_index()) {
        return m_db_connection->exec_prepared(
            "get_wkb", type_to_char(type), id_str.c_str());
    }
    return m_db_connection->exec_prepared("get_wkb", id_str);
}

void table_connection_t::delete_rows_with(osmium::item_type type, osmid_t id)
{
    m_copy_mgr.new_line(m_target);

    // If the table id type is some specific type, we don't care about the
    // type of the individual object, because they all will be the same.
    if (table().id_type() != osmium::item_type::undefined) {
        type = osmium::item_type::undefined;
    }
    m_copy_mgr.delete_object(type_to_char(type)[0], id);
}
