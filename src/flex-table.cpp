#include "flex-table.hpp"
#include "format.hpp"

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

static std::string tablespace_clause(const std::string &tablespace)
{
    if (tablespace.empty()) {
        return "";
    }

    return " TABLESPACE \"{}\" "_format(tablespace);
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
            return "PREPARE get_wkb (TEXT, BIGINT) AS"
                   " SELECT \"{}\" FROM {} WHERE \"{}\" = '$1' AND \"{}\" = $2"_format(
                       geom_column().name(), full_name(), m_columns[0].name(),
                       m_columns[1].name());
        }
        return "PREPARE get_wkb (BIGINT) AS"
               " SELECT \"{}\" FROM {} WHERE \"{}\" = $1"_format(
                   geom_column().name(), full_name(), id_column_names());
    }
    return "PREPARE get_wkb (BIGINT) AS SELECT ''";
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
        sql += " WITH ( autovacuum_enabled = FALSE )";
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

void flex_table_t::connect(std::string const &conninfo)
{
    assert(!m_db_connection);
    m_db_connection.reset(new pg_conn_t{conninfo});
    m_db_connection->exec("SET synchronous_commit TO off");
}

void flex_table_t::prepare()
{
    assert(m_db_connection);
    if (has_id_column()) {
        m_db_connection->exec(build_sql_prepare_get_wkb());
    }
}

void flex_table_t::start()
{
    m_db_connection->exec("SET client_min_messages = WARNING");

    if (!m_append) {
        m_db_connection->exec(
            "DROP TABLE IF EXISTS {} CASCADE"_format(full_name()));
    }

    // These _tmp tables can be left behind if we run out of disk space.
    m_db_connection->exec("DROP TABLE IF EXISTS {}"_format(full_tmp_name()));
    m_db_connection->exec("RESET client_min_messages");

    if (!m_append) {
        if (schema() != "public") {
            m_db_connection->exec(
                "CREATE SCHEMA IF NOT EXISTS \"{}\""_format(schema()));
        }
        m_db_connection->exec(build_sql_create_table(false));
    } else {
        //check the columns against those in the existing table
        auto const res = m_db_connection->query(
            PGRES_TUPLES_OK, "SELECT * FROM {} LIMIT 0"_format(full_name()));

        for (auto const &column : m_columns) {
            if (res.get_column_number(column.name()) < 0) {
                fmt::print(stderr, "Adding new column '{}' to '{}'\n",
                           column.name(), name());
                m_db_connection->exec(
                    "ALTER TABLE {} ADD COLUMN \"{}\" {}"_format(
                        full_name(), column.name(),
                        column.sql_type_name(m_srid)));
            }
            // Note: we do not verify the type or delete unused columns
        }

        //TODO: change the type of the geometry column if needed - this can only change to a more permissive type
    }

    prepare();
}

void flex_table_t::stop(bool updateable)
{
    if (m_append) {
        teardown();
        return;
    }

    std::time_t const start = std::time(nullptr);

    if (has_geom_column()) {
        fmt::print(stderr, "Clustering table '{}' by geometry...\n", name());

        // Notices about invalid geometries are expected and can be ignored
        // because they say nothing about the validity of the geometry in OSM.
        m_db_connection->exec("SET client_min_messages = WARNING");

        m_db_connection->exec(build_sql_create_table(true));

        std::string sql = "INSERT INTO {} SELECT * FROM {}"_format(
            full_tmp_name(), full_name());

        if (m_srid != 4326) {
            // libosmium assures validity of geometries in 4326.
            // Transformation to another projection could make the geometry
            // invalid. Therefore add a filter to drop those.
            sql += " WHERE ST_IsValid(\"{}\")"_format(geom_column().name());
        }

        auto const res = m_db_connection->query(
            PGRES_TUPLES_OK,
            "SELECT regexp_split_to_table(postgis_lib_version(), '\\.')");
        auto const postgis_major = std::stoi(res.get_value_as_string(0, 0));
        auto const postgis_minor = std::stoi(res.get_value_as_string(1, 0));

        sql += " ORDER BY ";
        if (postgis_major == 2 && postgis_minor < 4) {
            fmt::print(stderr, "Using GeoHash for clustering\n");
            if (m_srid == 4326) {
                sql += "ST_GeoHash({},10)"_format(geom_column().name());
            } else {
                sql +=
                    "ST_GeoHash(ST_Transform(ST_Envelope({}),4326),10)"_format(
                        geom_column().name());
            }
            sql += " COLLATE \"C\"";
        } else {
            fmt::print(stderr, "Using native order for clustering\n");
            // Since Postgis 2.4 the order function for geometries gives
            // useful results.
            sql += geom_column().name();
        }

        m_db_connection->exec(sql);

        m_db_connection->exec("DROP TABLE {}"_format(full_name()));
        m_db_connection->exec(
            "ALTER TABLE {} RENAME TO \"{}\""_format(full_tmp_name(), name()));

        fmt::print(stderr, "Creating geometry index on table '{}'...\n",
                   name());

        // Use fillfactor 100 for un-updateable imports
        m_db_connection->exec(
            "CREATE INDEX ON {} USING GIST (\"{}\") {} {}"_format(
                full_name(), geom_column().name(),
                (updateable ? "" : "WITH (FILLFACTOR=100)"),
                tablespace_clause(m_index_tablespace)));
    }

    if (updateable && has_id_column()) {
        fmt::print(stderr, "Creating id index on table '{}'...\n", name());
        create_id_index();

        if (m_srid != 4326 && has_geom_column()) {
            m_db_connection->exec(
                "CREATE OR REPLACE FUNCTION {}_osm2pgsql_valid()\n"
                "RETURNS TRIGGER AS $$\n"
                "BEGIN\n"
                "  IF ST_IsValid(NEW.{}) THEN \n"
                "    RETURN NEW;\n"
                "  END IF;\n"
                "  RETURN NULL;\n"
                "END;"
                "$$ LANGUAGE plpgsql;"_format(name(), geom_column().name()));

            m_db_connection->exec(
                "CREATE TRIGGER \"{0}_osm2pgsql_valid\""
                " BEFORE INSERT OR UPDATE"
                " ON {1}"
                " FOR EACH ROW EXECUTE PROCEDURE"
                " {0}_osm2pgsql_valid();"_format(name(), full_name()));
        }
    }

    fmt::print(stderr, "Analyzing table '{}'...\n", name());
    m_db_connection->exec("ANALYZE \"{}\""_format(name()));

    std::time_t const end = std::time(nullptr);
    fmt::print(stderr, "All postprocessing on table '{}' done in {}s.\n",
               name(), end - start);

    teardown();
}

void flex_table_t::create_id_index()
{
    m_db_connection->exec("CREATE INDEX ON {} USING BTREE ({}) {}"_format(
        full_name(), id_column_names(), tablespace_clause(m_index_tablespace)));
}

pg_result_t flex_table_t::get_geom_by_id(osmium::item_type type,
                                         osmid_t id) const
{
    assert(has_geom_column());
    assert(m_db_connection);
    std::string const id_str = fmt::to_string(id);
    if (has_multicolumn_id_index()) {
        char const *param_values[] = {type_to_char(type), id_str.c_str()};
        return m_db_connection->exec_prepared("get_wkb", 2, param_values);
    }
    char const *param_values[] = {id_str.c_str()};
    return m_db_connection->exec_prepared("get_wkb", 1, param_values);
}
