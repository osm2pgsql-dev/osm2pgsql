/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <cassert>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/types_from_string.hpp>

#include "format.hpp"
#include "logging.hpp"
#include "middle-pgsql.hpp"
#include "node-locations.hpp"
#include "node-persistent-cache.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "pgsql-helper.hpp"
#include "util.hpp"

static std::string build_sql(options_t const &options, char const *templ)
{
    std::string const using_tablespace{options.tblsslim_index.empty()
                                           ? ""
                                           : "USING INDEX TABLESPACE " +
                                                 options.tblsslim_index};
    return fmt::format(
        templ, fmt::arg("prefix", options.prefix),
        fmt::arg("schema", options.middle_dbschema.empty()
                               ? ""
                               : ("\"" + options.middle_dbschema + "\".")),
        fmt::arg("unlogged", options.droptemp ? "UNLOGGED" : ""),
        fmt::arg("using_tablespace", using_tablespace),
        fmt::arg("data_tablespace", tablespace_clause(options.tblsslim_data)),
        fmt::arg("index_tablespace", tablespace_clause(options.tblsslim_index)),
        fmt::arg("way_node_index_id_shift", options.way_node_index_id_shift));
}

middle_pgsql_t::table_desc::table_desc(options_t const &options,
                                       table_sql const &ts)
: m_create_table(build_sql(options, ts.create_table)),
  m_prepare_query(build_sql(options, ts.prepare_query)),
  m_copy_target(std::make_shared<db_target_descr_t>())
{
    m_copy_target->name = build_sql(options, ts.name);
    m_copy_target->schema = options.middle_dbschema;
    m_copy_target->id = "id"; // XXX hardcoded column name

    if (options.with_forward_dependencies) {
        m_prepare_fw_dep_lookups =
            build_sql(options, ts.prepare_fw_dep_lookups);
        m_create_fw_dep_indexes = build_sql(options, ts.create_fw_dep_indexes);
    }
}

void middle_query_pgsql_t::exec_sql(std::string const &sql_cmd) const
{
    m_sql_conn.exec(sql_cmd);
}

void middle_pgsql_t::table_desc::drop_table(
    pg_conn_t const &db_connection) const
{
    util::timer_t timer;

    log_info("Dropping table '{}'", name());

    auto const qual_name = qualified_name(schema(), name());
    db_connection.exec("DROP TABLE IF EXISTS {}"_format(qual_name));

    log_info("Table '{}' dropped in {}", name(),
             util::human_readable_duration(timer.stop()));
}

void middle_pgsql_t::table_desc::build_index(std::string const &conninfo) const
{
    if (m_create_fw_dep_indexes.empty()) {
        return;
    }

    // Use a temporary connection here because we might run in a separate
    // thread context.
    pg_conn_t db_connection{conninfo};

    log_info("Building index on table '{}'", name());
    db_connection.exec(m_create_fw_dep_indexes);
}

namespace {
// Decodes a portion of an array literal from postgres */
// Argument should point to beginning of literal, on return points to delimiter */
inline char const *decode_upto(char const *src, char *dst)
{
    bool const quoted = (*src == '"');
    if (quoted) {
        ++src;
    }

    while (quoted ? (*src != '"') : (*src != ',' && *src != '}')) {
        if (*src == '\\') {
            switch (src[1]) {
            case 'n':
                *dst++ = '\n';
                break;
            case 't':
                *dst++ = '\t';
                break;
            default:
                *dst++ = src[1];
                break;
            }
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    if (quoted) {
        ++src;
    }
    *dst = 0;
    return src;
}

template <typename T>
void pgsql_parse_tags(char const *string, osmium::memory::Buffer *buffer,
                      T *obuilder)
{
    if (*string++ != '{') {
        return;
    }

    char key[1024];
    char val[1024];
    osmium::builder::TagListBuilder builder{*buffer, obuilder};

    while (*string != '}') {
        string = decode_upto(string, key);
        // String points to the comma */
        ++string;
        string = decode_upto(string, val);
        builder.add_tag(key, val);
        // String points to the comma or closing '}' */
        if (*string == ',') {
            ++string;
        }
    }
}

void pgsql_parse_members(char const *string, osmium::memory::Buffer *buffer,
                         osmium::builder::RelationBuilder *obuilder)
{
    if (*string++ != '{') {
        return;
    }

    char role[1024];
    osmium::builder::RelationMemberListBuilder builder{*buffer, obuilder};

    while (*string != '}') {
        char type = string[0];
        char *endp = nullptr;
        osmid_t id = std::strtoll(string + 1, &endp, 10);
        // String points to the comma */
        string = decode_upto(endp + 1, role);
        builder.add_member(osmium::char_to_item_type(type), id, role);
        // String points to the comma or closing '}' */
        if (*string == ',') {
            ++string;
        }
    }
}

void pgsql_parse_nodes(char const *string, osmium::memory::Buffer *buffer,
                       osmium::builder::WayBuilder *obuilder)
{
    if (*string++ == '{') {
        osmium::builder::WayNodeListBuilder wnl_builder{*buffer, obuilder};
        while (*string != '}') {
            char *ptr = nullptr;
            wnl_builder.add_node_ref(std::strtoll(string, &ptr, 10));
            string = ptr;
            if (*string == ',') {
                ++string;
            }
        }
    }
}

} // anonymous namespace

void middle_pgsql_t::buffer_store_tags(osmium::OSMObject const &obj, bool attrs)
{
    if (obj.tags().empty() && !attrs) {
        m_db_copy.add_null_column();
    } else {
        m_db_copy.new_array();

        for (auto const &it : obj.tags()) {
            m_db_copy.add_array_elem(it.key());
            m_db_copy.add_array_elem(it.value());
        }

        if (attrs) {
            taglist_t extra;
            extra.add_attributes(obj);
            for (auto const &it : extra) {
                m_db_copy.add_array_elem(it.key);
                m_db_copy.add_array_elem(it.value);
            }
        }

        m_db_copy.finish_array();
    }
}

std::size_t middle_query_pgsql_t::get_way_node_locations_db(
    osmium::WayNodeList *nodes) const
{
    size_t count = 0;
    util::string_id_list_t id_list;

    // get nodes where possible from cache,
    // at the same time build a list for querying missing nodes from DB
    for (auto &n : *nodes) {
        auto const loc = m_cache->get(n.ref());
        if (loc.valid()) {
            n.set_location(loc);
            ++count;
        } else {
            id_list.add(n.ref());
        }
    }

    if (id_list.empty()) {
        return count;
    }

    // get any remaining nodes from the DB
    // Nodes must have been written back at this point.
    auto const res = m_sql_conn.exec_prepared("get_node_list", id_list.get());
    std::unordered_map<osmid_t, osmium::Location> locs;
    for (int i = 0; i < res.num_tuples(); ++i) {
        locs.emplace(
            osmium::string_to_object_id(res.get_value(i, 0)),
            osmium::Location{(int)strtol(res.get_value(i, 1), nullptr, 10),
                             (int)strtol(res.get_value(i, 2), nullptr, 10)});
    }

    for (auto &n : *nodes) {
        auto const el = locs.find(n.ref());
        if (el != locs.end()) {
            n.set_location(el->second);
            ++count;
        }
    }

    return count;
}

void middle_pgsql_t::node(osmium::Node const &node)
{
    if (node.deleted()) {
        node_delete(node.id());
    } else {
        if (m_options->append) {
            node_delete(node.id());
        }
        node_set(node);
    }
}

void middle_pgsql_t::way(osmium::Way const &way) {
    if (way.deleted()) {
        way_delete(way.id());
    } else {
        if (m_options->append) {
            way_delete(way.id());
        }
        way_set(way);
    }
}

void middle_pgsql_t::relation(osmium::Relation const &relation) {
    if (relation.deleted()) {
        relation_delete(relation.id());
    } else {
        if (m_options->append) {
            relation_delete(relation.id());
        }
        relation_set(relation);
    }
}

void middle_pgsql_t::node_set(osmium::Node const &node)
{
    m_cache->set(node.id(), node.location());

    if (!m_options->flat_node_file.empty()) {
        m_persistent_cache->set(node.id(), node.location());
    } else {
        m_db_copy.new_line(m_tables.nodes().copy_target());

        m_db_copy.add_columns(node.id(), node.location().y(),
                              node.location().x());

        m_db_copy.finish_line();
    }
}

std::size_t middle_query_pgsql_t::get_way_node_locations_flatnodes(
    osmium::WayNodeList *nodes) const
{
    std::size_t count = 0;

    for (auto &n : *nodes) {
        auto loc = m_cache->get(n.ref());
        if (!loc.valid() && n.ref() >= 0) {
            loc = m_persistent_cache->get(n.ref());
        }
        n.set_location(loc);
        if (loc.valid()) {
            ++count;
        }
    }

    return count;
}

size_t middle_query_pgsql_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    return m_persistent_cache ? get_way_node_locations_flatnodes(nodes)
                              : get_way_node_locations_db(nodes);
}

void middle_pgsql_t::node_delete(osmid_t osm_id)
{
    assert(m_options->append);

    if (!m_options->flat_node_file.empty()) {
        m_persistent_cache->set(osm_id, osmium::Location{});
    } else {
        m_db_copy.new_line(m_tables.nodes().copy_target());
        m_db_copy.delete_object(osm_id);
    }
}

idlist_t middle_pgsql_t::get_ways_by_node(osmid_t osm_id)
{
    return get_ids_from_db(&m_db_connection, "mark_ways_by_node", osm_id);
}

idlist_t middle_pgsql_t::get_rels_by_node(osmid_t osm_id)
{
    return get_ids_from_db(&m_db_connection, "mark_rels_by_node", osm_id);
}

idlist_t middle_pgsql_t::get_rels_by_way(osmid_t osm_id)
{
    return get_ids_from_db(&m_db_connection, "mark_rels_by_way", osm_id);
}

void middle_pgsql_t::way_set(osmium::Way const &way)
{
    m_db_copy.new_line(m_tables.ways().copy_target());

    m_db_copy.add_column(way.id());

    // nodes
    m_db_copy.new_array();
    for (auto const &n : way.nodes()) {
        m_db_copy.add_array_elem(n.ref());
    }
    m_db_copy.finish_array();

    buffer_store_tags(way, m_options->extra_attributes);

    m_db_copy.finish_line();
}

bool middle_query_pgsql_t::way_get(osmid_t id,
                                   osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    auto const res = m_sql_conn.exec_prepared("get_way", id);

    if (res.num_tuples() != 1) {
        return false;
    }

    {
        osmium::builder::WayBuilder builder{*buffer};
        builder.set_id(id);

        pgsql_parse_nodes(res.get_value(0, 0), buffer, &builder);
        pgsql_parse_tags(res.get_value(0, 1), buffer, &builder);
    }

    buffer->commit();

    return true;
}

std::size_t
middle_query_pgsql_t::rel_members_get(osmium::Relation const &rel,
                                      osmium::memory::Buffer *buffer,
                                      osmium::osm_entity_bits::type types) const
{
    assert(buffer);
    assert((types & osmium::osm_entity_bits::relation) == 0);

    util::string_id_list_t way_ids;
    idlist_t node_ids;

    for (auto const &member : rel.members()) {
        if (member.type() == osmium::item_type::node &&
            (types & osmium::osm_entity_bits::node)) {
            node_ids.push_back(member.ref());
        } else if (member.type() == osmium::item_type::way &&
                   (types & osmium::osm_entity_bits::way)) {
            way_ids.add(member.ref());
        }
    }

    std::size_t members_found = node_ids.size();
    if (!node_ids.empty()) {
        osmium::builder::WayNodeListBuilder builder{*buffer};
        for (auto const id : node_ids) {
            builder.add_node_ref(id);
        }
    }

    if (!way_ids.empty()) {
        auto const res =
            m_sql_conn.exec_prepared("get_way_list", way_ids.get());
        idlist_t const wayidspg = get_ids_from_result(res);

        // Match the list of ways coming from postgres in a different order
        //   back to the list of ways given by the caller
        for (auto const &m : rel.members()) {
            if (m.type() != osmium::item_type::way) {
                continue;
            }
            for (int j = 0; j < res.num_tuples(); ++j) {
                if (m.ref() == wayidspg[static_cast<std::size_t>(j)]) {
                    osmium::builder::WayBuilder builder{*buffer};
                    builder.set_id(m.ref());

                    pgsql_parse_nodes(res.get_value(j, 1), buffer, &builder);
                    pgsql_parse_tags(res.get_value(j, 2), buffer, &builder);

                    ++members_found;
                    break;
                }
            }
        }
    }

    buffer->commit();

    return members_found;
}

void middle_pgsql_t::way_delete(osmid_t osm_id)
{
    assert(m_options->append);
    m_db_copy.new_line(m_tables.ways().copy_target());
    m_db_copy.delete_object(osm_id);
}

void middle_pgsql_t::relation_set(osmium::Relation const &rel)
{
    // Sort relation members by their type.
    idlist_t parts[3];

    for (auto const &m : rel.members()) {
        parts[osmium::item_type_to_nwr_index(m.type())].push_back(m.ref());
    }

    m_db_copy.new_line(m_tables.relations().copy_target());

    // id, way offset, relation offset
    m_db_copy.add_columns(rel.id(), parts[0].size(),
                          parts[0].size() + parts[1].size());

    // parts
    m_db_copy.new_array();
    for (auto const &part : parts) {
        for (auto it : part) {
            m_db_copy.add_array_elem(it);
        }
    }
    m_db_copy.finish_array();

    // members
    if (rel.members().empty()) {
        m_db_copy.add_null_column();
    } else {
        m_db_copy.new_array();
        for (auto const &m : rel.members()) {
            m_db_copy.add_array_elem(osmium::item_type_to_char(m.type()) +
                                     std::to_string(m.ref()));
            m_db_copy.add_array_elem(m.role());
        }
        m_db_copy.finish_array();
    }

    // tags
    buffer_store_tags(rel, m_options->extra_attributes);

    m_db_copy.finish_line();
}

bool middle_query_pgsql_t::relation_get(osmid_t id,
                                        osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    auto const res = m_sql_conn.exec_prepared("get_rel", id);
    // Fields are: members, tags, member_count */
    //
    if (res.num_tuples() != 1) {
        return false;
    }

    {
        osmium::builder::RelationBuilder builder{*buffer};
        builder.set_id(id);

        pgsql_parse_members(res.get_value(0, 0), buffer, &builder);
        pgsql_parse_tags(res.get_value(0, 1), buffer, &builder);
    }

    buffer->commit();

    return true;
}

void middle_pgsql_t::relation_delete(osmid_t osm_id)
{
    assert(m_options->append);

    m_db_copy.new_line(m_tables.relations().copy_target());
    m_db_copy.delete_object(osm_id);
}

void middle_pgsql_t::after_nodes()
{
    m_db_copy.sync();
    if (!m_options->append && m_options->flat_node_file.empty()) {
        auto const &table = m_tables.nodes();
        analyze_table(m_db_connection, table.schema(), table.name());
    }
}

void middle_pgsql_t::after_ways()
{
    m_db_copy.sync();
    if (!m_options->append) {
        auto const &table = m_tables.ways();
        analyze_table(m_db_connection, table.schema(), table.name());
    }
}

void middle_pgsql_t::after_relations()
{
    m_db_copy.sync();
    if (!m_options->append) {
        auto const &table = m_tables.relations();
        analyze_table(m_db_connection, table.schema(), table.name());
    }

    // release the copy thread and its database connection
    m_copy_thread->finish();
}

middle_query_pgsql_t::middle_query_pgsql_t(
    std::string const &conninfo, std::shared_ptr<node_locations_t> cache,
    std::shared_ptr<node_persistent_cache> persistent_cache)
: m_sql_conn(conninfo), m_cache(std::move(cache)),
  m_persistent_cache(std::move(persistent_cache))
{
    // Disable JIT and parallel workers as they are known to cause
    // problems when accessing the intarrays.
    m_sql_conn.set_config("jit_above_cost", "-1");
    m_sql_conn.set_config("max_parallel_workers_per_gather", "0");
}

void middle_pgsql_t::start()
{
    if (m_options->append) {
        // Disable JIT and parallel workers as they are known to cause
        // problems when accessing the intarrays.
        m_db_connection.set_config("jit_above_cost", "-1");
        m_db_connection.set_config("max_parallel_workers_per_gather", "0");

        // Prepare queries for updating dependent objects
        for (auto &table : m_tables) {
            if (!table.m_prepare_fw_dep_lookups.empty()) {
                m_db_connection.exec(table.m_prepare_fw_dep_lookups);
            }
        }
    } else {
        m_db_connection.exec("SET client_min_messages = WARNING");
        for (auto const &table : m_tables) {
            log_debug("Setting up table '{}'", table.name());
            auto const qual_name = qualified_name(table.schema(), table.name());
            m_db_connection.exec(
                "DROP TABLE IF EXISTS {} CASCADE"_format(qual_name));
            m_db_connection.exec(table.m_create_table);
        }
    }
}

void middle_pgsql_t::stop()
{
    m_cache.reset();
    if (!m_options->flat_node_file.empty()) {
        m_persistent_cache.reset();
    }

    if (m_options->droptemp) {
        // Dropping the tables is fast, so do it synchronously to guarantee
        // that the space is freed before creating the other indices.
        for (auto &table : m_tables) {
            table.drop_table(m_db_connection);
        }
    } else if (!m_options->append) {
        // Building the indexes takes time, so do it asynchronously.
        for (auto &table : m_tables) {
            table.task_set(thread_pool().submit([&]() {
                table.build_index(m_options->database_options.conninfo());
            }));
        }
    }
}

void middle_pgsql_t::wait()
{
    for (auto &table : m_tables) {
        auto const run_time = table.task_wait();
        log_info("Done postprocessing on table '{}' in {}", table.name(),
                 util::human_readable_duration(run_time));
    }
}

static table_sql sql_for_nodes(bool create_table) noexcept
{
    table_sql sql{};

    sql.name = "{prefix}_nodes";

    if (create_table) {
        sql.create_table =
            "CREATE {unlogged} TABLE {schema}\"{prefix}_nodes\" ("
            "  id int8 PRIMARY KEY {using_tablespace},"
            "  lat int4 NOT NULL,"
            "  lon int4 NOT NULL"
            ") {data_tablespace};\n";

        sql.prepare_query =
            "PREPARE get_node_list(int8[]) AS"
            "  SELECT id, lon, lat FROM {schema}\"{prefix}_nodes\""
            "  WHERE id = ANY($1::int8[]);\n";
    }

    return sql;
}

static table_sql sql_for_ways(bool has_bucket_index,
                              uint8_t way_node_index_id_shift) noexcept
{
    table_sql sql{};

    sql.name = "{prefix}_ways";

    sql.create_table = "CREATE {unlogged} TABLE {schema}\"{prefix}_ways\" ("
                       "  id int8 PRIMARY KEY {using_tablespace},"
                       "  nodes int8[] NOT NULL,"
                       "  tags text[]"
                       ") {data_tablespace};\n";

    sql.prepare_query = "PREPARE get_way(int8) AS"
                        "  SELECT nodes, tags"
                        "    FROM {schema}\"{prefix}_ways\" WHERE id = $1;\n"
                        "PREPARE get_way_list(int8[]) AS"
                        "  SELECT id, nodes, tags"
                        "    FROM {schema}\"{prefix}_ways\""
                        "      WHERE id = ANY($1::int8[]);\n";

    if (has_bucket_index) {
        sql.prepare_fw_dep_lookups =
            "PREPARE mark_ways_by_node(int8) AS"
            "  SELECT id FROM {schema}\"{prefix}_ways\" w"
            "    WHERE $1 = ANY(nodes)"
            "      AND {schema}\"{prefix}_index_bucket\"(w.nodes)"
            "       && {schema}\"{prefix}_index_bucket\"(ARRAY[$1]);\n";
    } else {
        sql.prepare_fw_dep_lookups =
            "PREPARE mark_ways_by_node(int8) AS"
            "  SELECT id FROM {schema}\"{prefix}_ways\""
            "    WHERE nodes && ARRAY[$1];\n";
    }

    if (way_node_index_id_shift == 0) {
        sql.create_fw_dep_indexes =
            "CREATE INDEX ON {schema}\"{prefix}_ways\" USING GIN (nodes)"
            "  WITH (fastupdate = off) {index_tablespace};\n";
    } else {
        sql.create_fw_dep_indexes =
            "CREATE OR REPLACE FUNCTION"
            "    {schema}\"{prefix}_index_bucket\"(int8[])"
            "  RETURNS int8[] AS $$\n"
            "  SELECT ARRAY(SELECT DISTINCT"
            "    unnest($1) >> {way_node_index_id_shift})\n"
            "$$ LANGUAGE SQL IMMUTABLE;\n"
            "CREATE INDEX \"{prefix}_ways_nodes_bucket_idx\""
            "  ON {schema}\"{prefix}_ways\""
            "  USING GIN ({schema}\"{prefix}_index_bucket\"(nodes))"
            "  WITH (fastupdate = off) {index_tablespace};\n";
    }

    return sql;
}

static table_sql sql_for_relations() noexcept
{
    table_sql sql{};

    sql.name = "{prefix}_rels";

    sql.create_table = "CREATE {unlogged} TABLE {schema}\"{prefix}_rels\" ("
                       "  id int8 PRIMARY KEY {using_tablespace},"
                       "  way_off int2,"
                       "  rel_off int2,"
                       "  parts int8[],"
                       "  members text[],"
                       "  tags text[]"
                       ") {data_tablespace};\n";

    sql.prepare_query = "PREPARE get_rel(int8) AS"
                        "  SELECT members, tags"
                        "    FROM {schema}\"{prefix}_rels\" WHERE id = $1;\n";

    sql.prepare_fw_dep_lookups =
        "PREPARE mark_rels_by_node(int8) AS"
        "  SELECT id FROM {schema}\"{prefix}_rels\""
        "    WHERE parts && ARRAY[$1]"
        "      AND parts[1:way_off] && ARRAY[$1];\n"
        "PREPARE mark_rels_by_way(int8) AS"
        "  SELECT id FROM {schema}\"{prefix}_rels\""
        "    WHERE parts && ARRAY[$1]"
        "      AND parts[way_off+1:rel_off] && ARRAY[$1];\n";

    sql.create_fw_dep_indexes =
        "CREATE INDEX ON {schema}\"{prefix}_rels\" USING GIN (parts)"
        "  WITH (fastupdate = off) {index_tablespace};\n";

    return sql;
}

static bool check_bucket_index(pg_conn_t *db_connection,
                               std::string const &prefix)
{
    auto const res = db_connection->query(
        PGRES_TUPLES_OK,
        "SELECT relname FROM pg_class WHERE relkind='i' AND"
        "  relname = '{}_ways_nodes_bucket_idx';"_format(prefix));
    return res.num_tuples() > 0;
}

middle_pgsql_t::middle_pgsql_t(std::shared_ptr<thread_pool_t> thread_pool,
                               options_t const *options)
: middle_t(std::move(thread_pool)), m_options(options),
  m_cache(std::make_unique<node_locations_t>(
      static_cast<std::size_t>(options->cache) * 1024UL * 1024UL)),
  m_db_connection(m_options->database_options.conninfo()),
  m_copy_thread(
      std::make_shared<db_copy_thread_t>(options->database_options.conninfo())),
  m_db_copy(m_copy_thread)
{
    if (!options->flat_node_file.empty()) {
        m_persistent_cache = std::make_shared<node_persistent_cache>(
            options->flat_node_file, options->droptemp);
    }

    log_debug("Mid: pgsql, cache={}", options->cache);

    bool const has_bucket_index =
        check_bucket_index(&m_db_connection, options->prefix);

    if (!has_bucket_index && options->append &&
        options->with_forward_dependencies) {
        log_debug("You don't have a bucket index. See manual for details.");
    }

    m_tables.nodes() =
        table_desc{*options, sql_for_nodes(options->flat_node_file.empty())};
    m_tables.ways() =
        table_desc{*options, sql_for_ways(has_bucket_index,
                                          options->way_node_index_id_shift)};
    m_tables.relations() = table_desc{*options, sql_for_relations()};
}

std::shared_ptr<middle_query_t>
middle_pgsql_t::get_query_instance()
{
    // NOTE: this is thread safe for use in pending async processing only because
    // during that process they are only read from
    auto mid = std::make_unique<middle_query_pgsql_t>(
        m_options->database_options.conninfo(), m_cache, m_persistent_cache);

    // We use a connection per table to enable the use of COPY
    for (auto &table : m_tables) {
        mid->exec_sql(table.m_prepare_query);
    }

    return std::shared_ptr<middle_query_t>(mid.release());
}
