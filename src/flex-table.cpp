#include "flex-table.hpp"
#include "format.hpp"
#include "logging.hpp"
#include "pgsql-helper.hpp"
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
    return qualified_name(schema(), name());
}

std::string flex_table_t::full_tmp_name() const
{
    return qualified_name(schema(), name() + "_tmp");
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
    if (has_multicolumn_id_index()) {
        return "PREPARE get_wkb(char(1), bigint) AS"
               " SELECT \"{}\" FROM {} WHERE \"{}\" = $1 AND \"{}\" = $2"_format(
                   geom_column().name(), full_name(), m_columns[0].name(),
                   m_columns[1].name());
    }

    return "PREPARE get_wkb(bigint) AS"
           " SELECT \"{}\" FROM {} WHERE \"{}\" = $1"_format(
               geom_column().name(), full_name(), id_column_names());
}

std::string
flex_table_t::build_sql_create_table(table_type ttype,
                                     std::string const &table_name) const
{
    assert(!m_columns.empty());

    std::string sql = "CREATE {} TABLE IF NOT EXISTS {} ("_format(
        ttype == table_type::interim ? "UNLOGGED" : "", table_name);

    for (auto const &column : m_columns) {
        sql += column.sql_create();
    }

    assert(sql.back() == ',');
    sql.back() = ')';

    if (ttype == table_type::interim) {
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
        m_db_connection->exec(table().build_sql_create_table(
            table().has_geom_column() ? flex_table_t::table_type::interim
                                      : flex_table_t::table_type::permanent,
            table().full_name()));
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
        log_info("Clustering table '{}' by geometry...", table().name());

        // Notices about invalid geometries are expected and can be ignored
        // because they say nothing about the validity of the geometry in OSM.
        m_db_connection->exec("SET client_min_messages = WARNING");

        m_db_connection->exec(table().build_sql_create_table(
            flex_table_t::table_type::permanent, table().full_tmp_name()));

        std::string sql = "INSERT INTO {} SELECT * FROM {}"_format(
            table().full_tmp_name(), table().full_name());

        if (table().geom_column().srid() != 4326) {
            // libosmium assures validity of geometries in 4326.
            // Transformation to another projection could make the geometry
            // invalid. Therefore add a filter to drop those.
            sql += " WHERE ST_IsValid(\"{}\")"_format(
                table().geom_column().name());
        }

        auto const postgis_version = get_postgis_version(*m_db_connection);

        sql += " ORDER BY ";
        if (postgis_version.major == 2 && postgis_version.minor < 4) {
            log_info("Using GeoHash for clustering");
            if (table().geom_column().srid() == 4326) {
                sql += "ST_GeoHash({},10)"_format(table().geom_column().name());
            } else {
                sql +=
                    "ST_GeoHash(ST_Transform(ST_Envelope({}),4326),10)"_format(
                        table().geom_column().name());
            }
            sql += " COLLATE \"C\"";
        } else {
            log_info("Using native order for clustering");
            // Since Postgis 2.4 the order function for geometries gives
            // useful results.
            sql += table().geom_column().name();
        }

        m_db_connection->exec(sql);

        m_db_connection->exec("DROP TABLE {}"_format(table().full_name()));
        m_db_connection->exec("ALTER TABLE {} RENAME TO \"{}\""_format(
            table().full_tmp_name(), table().name()));
        m_id_index_created = false;

        log_info("Creating geometry index on table '{}'...", table().name());

        // Use fillfactor 100 for un-updateable imports
        m_db_connection->exec(
            "CREATE INDEX ON {} USING GIST (\"{}\") {} {}"_format(
                table().full_name(), table().geom_column().name(),
                (updateable ? "" : "WITH (fillfactor = 100)"),
                tablespace_clause(table().index_tablespace())));
    }

    if (updateable && table().has_id_column()) {
        create_id_index();

        if (table().has_geom_column() && table().geom_column().srid() != 4326) {
            create_geom_check_trigger(m_db_connection.get(), table().schema(),
                                      table().name(),
                                      table().geom_column().name());
        }
    }

    log_info("Analyzing table '{}'...", table().name());
    m_db_connection->exec("ANALYZE " + table().full_name());

    log_info("All postprocessing on table '{}' done in {}.", table().name(),
             util::human_readable_duration(timer.stop()));

    teardown();
}

void table_connection_t::prepare()
{
    assert(m_db_connection);
    if (table().has_id_column() && table().has_geom_column()) {
        m_db_connection->exec(table().build_sql_prepare_get_wkb());
    }
}

void table_connection_t::create_id_index()
{
    if (m_id_index_created) {
        log_info("Id index on table '{}' already created.", table().name());
    } else {
        log_info("Creating id index on table '{}'...", table().name());
        m_db_connection->exec(table().build_sql_create_id_index());
        m_id_index_created = true;
    }
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

    if (!table().has_multicolumn_id_index()) {
        type = osmium::item_type::undefined;
    }
    m_copy_mgr.delete_object(type_to_char(type)[0], id);
}
