/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/types_from_string.hpp>

#include <nlohmann/json.hpp>

#include "format.hpp"
#include "idlist.hpp"
#include "json-writer.hpp"
#include "logging.hpp"
#include "middle-pgsql.hpp"
#include "node-locations.hpp"
#include "node-persistent-cache.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "pgsql-helper.hpp"
#include "template.hpp"
#include "util.hpp"

namespace {

void send_id_list(pg_conn_t const &db_connection, std::string const &table,
                  idlist_t const &ids)
{
    std::string data;
    for (auto const id : ids) {
        fmt::format_to(std::back_inserter(data), FMT_STRING("{}\n"), id);
    }

    auto const sql = fmt::format("COPY {} FROM STDIN", table);
    db_connection.copy_start(sql);
    db_connection.copy_send(data, table);
    db_connection.copy_end(table);
}

void load_id_list(pg_conn_t const &db_connection, std::string const &table,
                  idlist_t *ids)
{
    auto const res = db_connection.exec(
        fmt::format("SELECT DISTINCT id FROM {} ORDER BY id", table));
    for (int n = 0; n < res.num_tuples(); ++n) {
        ids->push_back(osmium::string_to_object_id(res.get_value(n, 0)));
    }
}

} // anonymous namespace

middle_pgsql_t::table_desc::table_desc(options_t const &options,
                                       std::string_view name)
: m_copy_target(std::make_shared<db_target_descr_t>(
      options.middle_dbschema, fmt::format("{}_{}", options.prefix, name),
      "id"))
{
}

std::string middle_pgsql_t::render_template(std::string_view templ) const
{
    template_t sql_template{templ};
    sql_template.set_params(m_params);
    return sql_template.render();
}

void middle_pgsql_t::dbexec(std::string_view templ) const
{
    m_db_connection.exec(render_template(templ));
}

void middle_query_pgsql_t::prepare(std::string const &stmt,
                                   std::string const &sql_cmd) const
{
    m_db_connection.prepare(stmt, fmt::runtime(sql_cmd));
}

void middle_pgsql_t::table_desc::drop_table(
    pg_conn_t const &db_connection) const
{
    util::timer_t timer;

    log_info("Dropping table '{}'", name());
    drop_table_if_exists(db_connection, schema(), name());
    log_info("Table '{}' dropped in {}", name(),
             util::human_readable_duration(timer.stop()));
}

void middle_pgsql_t::table_desc::init_max_id(pg_conn_t const &db_connection)
{
    auto const qual_name = qualified_name(schema(), name());
    auto const res = db_connection.exec("SELECT max(id) FROM {}", qual_name);

    if (res.is_null(0, 0)) {
        return;
    }

    m_max_id = osmium::string_to_object_id(res.get_value(0, 0));
}

namespace {

/**
 * Parse JSON-encoded tags from a version 2 middle table and add them to the
 * builder.
 */
template <typename T>
void pgsql_parse_json_tags(char const *string, osmium::memory::Buffer *buffer,
                           T *obuilder)
{
    if (*string == '\0') { // NULL
        return;
    }

    auto const tags = nlohmann::json::parse(string);
    if (!tags.is_object()) {
        throw std::runtime_error{"Database format for tags invalid."};
    }

    // These will come out sorted, because internally "tags" uses a std::map.
    osmium::builder::TagListBuilder builder{*buffer, obuilder};
    for (auto const &tag : tags.items()) {
        builder.add_tag(tag.key(), tag.value());
    }
}

/**
 * Helper class for parsing relation members encoded in JSON.
 */
class member_list_json_builder
{
public:
    explicit member_list_json_builder(
        osmium::builder::RelationMemberListBuilder *builder)
    : m_builder(builder)
    {}

    static bool number_integer(nlohmann::json::number_integer_t /*val*/)
    {
        return true;
    }

    bool number_unsigned(nlohmann::json::number_unsigned_t val)
    {
        m_ref = static_cast<osmium::object_id_type>(val);
        return true;
    }

    static bool number_float(nlohmann::json::number_float_t /*val*/,
                             nlohmann::json::string_t const & /*s*/)
    {
        return true;
    }

    bool key(nlohmann::json::string_t &val)
    {
        if (val == "type") {
            m_next_val = next_val::type;
        } else if (val == "ref") {
            m_next_val = next_val::ref;
        } else if (val == "role") {
            m_next_val = next_val::role;
        } else {
            m_next_val = next_val::none;
        }
        return true;
    }

    bool string(nlohmann::json::string_t &val)
    {
        if (m_next_val == next_val::type && val.size() == 1) {
            switch (val[0]) {
            case 'N':
                m_type = osmium::item_type::node;
                break;
            case 'W':
                m_type = osmium::item_type::way;
                break;
            default:
                m_type = osmium::item_type::relation;
                break;
            }
        } else if (m_next_val == next_val::role) {
            m_role = val;
        }
        return true;
    }

    bool end_object()
    {
        m_builder->add_member(m_type, m_ref, m_role);
        m_next_val = next_val::none;
        m_type = osmium::item_type::undefined;
        m_ref = 0;
        m_role.clear();
        return true;
    }

    static bool null() { return true; }
    static bool boolean(bool /*val*/) { return true; }
    static bool binary(std::vector<std::uint8_t> & /*val*/) { return true; }
    static bool start_object(std::size_t /*elements*/) { return true; }
    static bool start_array(std::size_t /*elements*/) { return true; }
    static bool end_array() { return true; }

    static bool parse_error(std::size_t /*position*/,
                            std::string const & /*last_token*/,
                            nlohmann::json::exception const &ex)
    {
        throw ex;
    }

private:
    std::string m_role;
    osmium::builder::RelationMemberListBuilder *m_builder;
    osmium::object_id_type m_ref = 0;
    osmium::item_type m_type = osmium::item_type::undefined;
    enum class next_val : std::uint8_t
    {
        none,
        type,
        ref,
        role
    } m_next_val = next_val::none;
}; // class member_list_json_builder

template <typename T>
void pgsql_parse_json_members(char const *string,
                              osmium::memory::Buffer *buffer, T *obuilder)
{
    if (*string == '\0') { // NULL
        return;
    }

    osmium::builder::RelationMemberListBuilder builder{*buffer, obuilder};
    member_list_json_builder parser{&builder};
    nlohmann::json::sax_parse(string, &parser);
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

template <typename T>
void set_attributes_on_builder(T *builder, pg_result_t const &result, int num,
                               int offset)
{
    if (!result.is_null(num, offset + 2)) {
        builder->set_timestamp(
            std::strtoul(result.get_value(num, offset + 2), nullptr, 10));
    }
    if (!result.is_null(num, offset + 3)) {
        builder->set_version(result.get_value(num, offset + 3));
    }
    if (!result.is_null(num, offset + 4)) {
        builder->set_changeset(result.get_value(num, offset + 4));
    }
    if (!result.is_null(num, offset + 5)) {
        builder->set_uid(result.get_value(num, offset + 5));
    }
    if (!result.is_null(num, offset + 6)) {
        builder->set_user(result.get_value(num, offset + 6));
    }
}

void tags_to_json(osmium::TagList const &tags, json_writer_t *writer)
{
    writer->start_object();

    for (auto const &tag : tags) {
        writer->key(tag.key());
        writer->string(tag.value());
        writer->next();
    }

    writer->end_object();
}

void members_to_json(osmium::RelationMemberList const &members,
                     json_writer_t *writer)
{
    writer->start_array();

    for (auto const &member : members) {
        writer->start_object();

        writer->key("type");
        switch (member.type()) {
        case osmium::item_type::node:
            writer->string("N");
            break;
        case osmium::item_type::way:
            writer->string("W");
            break;
        default: // osmium::item_type::relation
            writer->string("R");
            break;
        }
        writer->next();

        writer->key("ref");
        writer->number(member.ref());
        writer->next();

        writer->key("role");
        writer->string(member.role());
        writer->end_object();

        writer->next();
    }

    writer->end_array();
}

} // anonymous namespace

void middle_pgsql_t::copy_attributes(osmium::OSMObject const &obj)
{
    if (obj.timestamp()) {
        m_db_copy.add_column(obj.timestamp().to_iso());
    } else {
        m_db_copy.add_null_column();
    }

    if (obj.version()) {
        m_db_copy.add_column(obj.version());
    } else {
        m_db_copy.add_null_column();
    }

    if (obj.changeset()) {
        m_db_copy.add_columns(obj.changeset());
    } else {
        m_db_copy.add_null_column();
    }

    if (obj.uid()) {
        m_db_copy.add_columns(obj.uid());
        m_users.try_emplace(obj.uid(), obj.user());
    } else {
        m_db_copy.add_null_column();
    }
}

void middle_pgsql_t::copy_tags(osmium::OSMObject const &obj)
{
    if (obj.tags().empty()) {
        m_db_copy.add_null_column();
        return;
    }
    json_writer_t writer;
    tags_to_json(obj.tags(), &writer);
    m_db_copy.add_column(writer.json());
}

std::size_t middle_query_pgsql_t::get_way_node_locations_db(
    osmium::WayNodeList *nodes) const
{
    size_t count = 0;
    util::string_joiner_t id_list{',', '\0', '{', '}'};

    // get nodes where possible from cache,
    // at the same time build a list for querying missing nodes from DB
    for (auto &n : *nodes) {
        auto const loc = m_cache->get(n.ref());
        if (loc.valid()) {
            n.set_location(loc);
            ++count;
        } else {
            id_list.add(fmt::to_string(n.ref()));
        }
    }

    if (id_list.empty()) {
        return count;
    }

    // get any remaining nodes from the DB
    // Nodes must have been written back at this point.
    auto const res = m_db_connection.exec_prepared("get_node_list", id_list());
    std::unordered_map<osmid_t, osmium::Location> locs;
    for (int i = 0; i < res.num_tuples(); ++i) {
        locs.emplace(osmium::string_to_object_id(res.get_value(i, 0)),
                     osmium::Location{
                         (int)std::strtol(res.get_value(i, 1), nullptr, 10),
                         (int)std::strtol(res.get_value(i, 2), nullptr, 10)});
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
    assert(m_middle_state == middle_state::node);

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
    assert(m_middle_state == middle_state::way);

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
    assert(m_middle_state == middle_state::relation);

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

    if (m_persistent_cache) {
        m_persistent_cache->set(node.id(), node.location());
    }

    if (!m_store_options.nodes) {
        return;
    }

    if (!m_store_options.untagged_nodes && node.tags().empty()) {
        return;
    }

    m_db_copy.new_line(m_tables.nodes().copy_target());

    m_db_copy.add_columns(node.id(), node.location().y(), node.location().x());

    if (m_store_options.with_attributes) {
        copy_attributes(node);
    }
    copy_tags(node);

    m_db_copy.finish_line();
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

osmium::Location middle_query_pgsql_t::get_node_location_db(osmid_t id) const
{
    auto const res = m_db_connection.exec_prepared("get_node_location", id);
    if (res.num_tuples() == 0) {
        return osmium::Location{};
    }

    return osmium::Location{(int)std::strtol(res.get_value(0, 1), nullptr, 10),
                            (int)std::strtol(res.get_value(0, 2), nullptr, 10)};
}

osmium::Location
middle_query_pgsql_t::get_node_location_flatnodes(osmid_t id) const
{
    if (id >= 0) {
        return m_persistent_cache->get(id);
    }
    return osmium::Location{};
}

osmium::Location middle_query_pgsql_t::get_node_location(osmid_t id) const
{
    auto const loc = m_cache->get(id);
    if (loc.valid()) {
        return loc;
    }

    return m_persistent_cache ? get_node_location_flatnodes(id)
                              : get_node_location_db(id);
}

size_t middle_query_pgsql_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    return m_persistent_cache ? get_way_node_locations_flatnodes(nodes)
                              : get_way_node_locations_db(nodes);
}

void middle_pgsql_t::node_delete(osmid_t osm_id)
{
    assert(m_options->append);

    if (m_persistent_cache) {
        m_persistent_cache->set(osm_id, osmium::Location{});
    }

    if (m_store_options.nodes && osm_id <= m_tables.nodes().max_id()) {
        m_db_copy.new_line(m_tables.nodes().copy_target());
        m_db_copy.delete_object(osm_id);
    }
}

void middle_pgsql_t::get_node_parents(idlist_t const &changed_nodes,
                                      idlist_t *parent_ways,
                                      idlist_t *parent_relations) const
{
    util::timer_t timer;

    m_db_connection.exec("BEGIN");
    m_db_connection.exec("CREATE TEMP TABLE osm2pgsql_changed_nodes"
                         " (id int8 NOT NULL) ON COMMIT DROP");
    m_db_connection.exec("CREATE TEMP TABLE osm2pgsql_changed_ways"
                         " (id int8 NOT NULL) ON COMMIT DROP");
    m_db_connection.exec("CREATE TEMP TABLE osm2pgsql_changed_relations"
                         " (id int8 NOT NULL) ON COMMIT DROP");

    send_id_list(m_db_connection, "osm2pgsql_changed_nodes", changed_nodes);

    std::vector<std::string> queries;

    queries.emplace_back("ANALYZE osm2pgsql_changed_nodes");

    // The query to get the parent ways of changed nodes is "hidden"
    // inside a PL/pgSQL function so that the query planner only sees
    // a single node id that is being queried for. If we ask for all
    // nodes at the same time the query planner sometimes thinks it is
    // better to do a full table scan which totally destroys performance.
    // This is due to the PostgreSQL statistics on ARRAYs being way off.
    queries.emplace_back(R"(
CREATE OR REPLACE FUNCTION {schema}osm2pgsql_find_changed_ways() RETURNS void AS $$
DECLARE
  changed_buckets RECORD;
BEGIN
  FOR changed_buckets IN
    SELECT array_agg(id) AS node_ids, id >> {way_node_index_id_shift} AS bucket
      FROM osm2pgsql_changed_nodes GROUP BY id >> {way_node_index_id_shift}
  LOOP
    INSERT INTO osm2pgsql_changed_ways
    SELECT DISTINCT w.id
      FROM {schema}"{prefix}_ways" w
      WHERE w.nodes && changed_buckets.node_ids
        AND {schema}"{prefix}_index_bucket"(w.nodes)
         && ARRAY[changed_buckets.bucket];
  END LOOP;
END;
$$ LANGUAGE plpgsql
)");
    queries.emplace_back("SELECT {schema}osm2pgsql_find_changed_ways()");
    queries.emplace_back("DROP FUNCTION {schema}osm2pgsql_find_changed_ways()");

    queries.emplace_back(R"(
INSERT INTO osm2pgsql_changed_relations
  SELECT r.id
    FROM {schema}"{prefix}_rels" r, osm2pgsql_changed_nodes c
    WHERE {schema}"{prefix}_member_ids"(r.members, 'N'::char) && ARRAY[c.id];
    )");

    for (auto const &query : queries) {
        dbexec(query);
    }

    if (parent_ways) {
        load_id_list(m_db_connection, "osm2pgsql_changed_ways", parent_ways);
    }

    load_id_list(m_db_connection, "osm2pgsql_changed_relations",
                 parent_relations);

    m_db_connection.exec("COMMIT");

    timer.stop();

    log_debug("Found {} new/changed nodes in input.", changed_nodes.size());

    auto const elapsed_sec =
        std::chrono::duration_cast<std::chrono::seconds>(timer.elapsed());

    if (parent_ways) {
        log_debug("  Found in {} their {} parent ways and {} parent relations.",
                  elapsed_sec, parent_ways->size(), parent_relations->size());
    } else {
        log_debug("  Found in {} their {} parent relations.", elapsed_sec,
                  parent_relations->size());
    }
}

void middle_pgsql_t::get_way_parents(idlist_t const &changed_ways,
                                     idlist_t *parent_relations) const
{
    util::timer_t timer;

    auto const num_relations_referenced_by_nodes = parent_relations->size();

    m_db_connection.exec("BEGIN");
    m_db_connection.exec("CREATE TEMP TABLE osm2pgsql_changed_ways"
                         " (id int8 NOT NULL) ON COMMIT DROP");
    m_db_connection.exec("CREATE TEMP TABLE osm2pgsql_changed_relations"
                         " (id int8 NOT NULL) ON COMMIT DROP");

    send_id_list(m_db_connection, "osm2pgsql_changed_ways", changed_ways);

    m_db_connection.exec("ANALYZE osm2pgsql_changed_ways");

    dbexec(R"(
INSERT INTO osm2pgsql_changed_relations
  SELECT DISTINCT r.id
    FROM {schema}"{prefix}_rels" r, osm2pgsql_changed_ways c
    WHERE {schema}"{prefix}_member_ids"(r.members, 'W'::char) && ARRAY[c.id];
    )");

    load_id_list(m_db_connection, "osm2pgsql_changed_relations",
                 parent_relations);

    m_db_connection.exec("COMMIT");

    timer.stop();
    log_debug("Found {} ways that are new/changed in input or parent of"
              " changed node.",
              changed_ways.size());
    log_debug("  Found in {} their {} parent relations.",
              std::chrono::duration_cast<std::chrono::seconds>(timer.elapsed()),
              parent_relations->size() - num_relations_referenced_by_nodes);

    // (Potentially) contains parent relations from nodes and from ways. Make
    // sure they are merged.
    parent_relations->sort_unique();
}

void middle_pgsql_t::way_set(osmium::Way const &way)
{
    m_db_copy.new_line(m_tables.ways().copy_target());

    m_db_copy.add_column(way.id());

    if (m_store_options.with_attributes) {
        copy_attributes(way);
    }

    // nodes
    m_db_copy.new_array();
    for (auto const &n : way.nodes()) {
        m_db_copy.add_array_elem(n.ref());
    }
    m_db_copy.finish_array();

    copy_tags(way);

    m_db_copy.finish_line();
}

namespace {

/**
 * Build node in buffer from database results.
 */
void build_node(osmid_t id, pg_result_t const &res, int res_num, int offset,
                osmium::memory::Buffer *buffer, bool with_attributes)
{
    osmium::builder::NodeBuilder builder{*buffer};
    builder.set_id(id);
    builder.set_location(osmium::Location{
        (int)std::strtol(res.get_value(res_num, offset + 0), nullptr, 10),
        (int)std::strtol(res.get_value(res_num, offset + 1), nullptr, 10)});

    if (with_attributes) {
        set_attributes_on_builder(&builder, res, res_num, offset + 3);
    }
    pgsql_parse_json_tags(res.get_value(res_num, offset + 2), buffer, &builder);
}

/**
 * Build way in buffer from database results.
 */
void build_way(osmid_t id, pg_result_t const &res, int res_num, int offset,
               osmium::memory::Buffer *buffer, bool with_attributes)
{
    osmium::builder::WayBuilder builder{*buffer};
    builder.set_id(id);

    if (with_attributes) {
        set_attributes_on_builder(&builder, res, res_num, offset);
    }
    pgsql_parse_nodes(res.get_value(res_num, offset + 0), buffer, &builder);
    pgsql_parse_json_tags(res.get_value(res_num, offset + 1), buffer, &builder);
}

} // anonymous namespace

bool middle_query_pgsql_t::node_get(osmid_t id,
                                    osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    if (m_store_options.nodes) {
        auto const res = m_db_connection.exec_prepared("get_node", id);

        if (res.num_tuples() == 1) {
            build_node(id, res, 0, 0, buffer, m_store_options.with_attributes);
            buffer->commit();
            return true;
        }
    }

    if (m_store_options.use_flat_node_file) {
        auto const location = get_node_location_flatnodes(id);
        if (!location.valid()) {
            return false;
        }

        {
            osmium::builder::NodeBuilder builder{*buffer};
            builder.set_id(id);
            builder.set_location(location);
        }

        buffer->commit();
        return true;
    }

    return false;
}

bool middle_query_pgsql_t::way_get(osmid_t id,
                                   osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    auto const res = m_db_connection.exec_prepared("get_way", id);

    if (res.num_tuples() != 1) {
        return false;
    }

    build_way(id, res, 0, 0, buffer, m_store_options.with_attributes);

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

    pg_result_t res;
    idlist_t wayidspg;
    if (types & osmium::osm_entity_bits::way) {
        // collect ids from all way members into a list..
        util::string_joiner_t way_ids{',', '\0', '{', '}'};
        for (auto const &member : rel.members()) {
            if (member.type() == osmium::item_type::way) {
                way_ids.add(fmt::to_string(member.ref()));
            }
        }

        // ...and get those ways from database
        if (!way_ids.empty()) {
            res = m_db_connection.exec_prepared("get_way_list", way_ids());
            wayidspg = get_ids_from_result(res);
        }
    }

    std::size_t members_found = 0;
    for (auto const &member : rel.members()) {
        if (member.type() == osmium::item_type::node &&
            (types & osmium::osm_entity_bits::node)) {
            if (node_get(member.ref(), buffer)) {
                ++members_found;
            }
        } else if (member.type() == osmium::item_type::way &&
                   (types & osmium::osm_entity_bits::way) && res) {
            // Match the list of ways coming from postgres in a different order
            // back to the list of ways given by the caller
            for (int j = 0; j < res.num_tuples(); ++j) {
                if (member.ref() == wayidspg[static_cast<std::size_t>(j)]) {
                    build_way(member.ref(), res, j, 1, buffer,
                              m_store_options.with_attributes);
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

    if (osm_id <= m_tables.ways().max_id()) {
        m_db_copy.new_line(m_tables.ways().copy_target());
        m_db_copy.delete_object(osm_id);
    }
}

void middle_pgsql_t::relation_set(osmium::Relation const &rel)
{
    m_db_copy.new_line(m_tables.relations().copy_target());
    m_db_copy.add_column(rel.id());

    if (m_store_options.with_attributes) {
        copy_attributes(rel);
    }

    json_writer_t writer;
    members_to_json(rel.members(), &writer);
    m_db_copy.add_column(writer.json());

    copy_tags(rel);

    m_db_copy.finish_line();
}

bool middle_query_pgsql_t::relation_get(osmid_t id,
                                        osmium::memory::Buffer *buffer) const
{
    assert(buffer);

    auto const res = m_db_connection.exec_prepared("get_rel", id);

    if (res.num_tuples() == 0) {
        return false;
    }

    {
        osmium::builder::RelationBuilder builder{*buffer};
        builder.set_id(id);

        if (m_store_options.with_attributes) {
            set_attributes_on_builder(&builder, res, 0, 0);
        }

        pgsql_parse_json_members(res.get_value(0, 0), buffer, &builder);
        pgsql_parse_json_tags(res.get_value(0, 1), buffer, &builder);
    }
    buffer->commit();

    return true;
}

void middle_pgsql_t::relation_delete(osmid_t osm_id)
{
    assert(m_options->append);

    if (osm_id <= m_tables.relations().max_id()) {
        m_db_copy.new_line(m_tables.relations().copy_target());
        m_db_copy.delete_object(osm_id);
    }
}

void middle_pgsql_t::after_nodes()
{
    assert(m_middle_state == middle_state::node);
#ifndef NDEBUG
    m_middle_state = middle_state::way;
#endif

    m_db_copy.sync();
    if (!m_options->append && m_store_options.nodes) {
        auto const &table = m_tables.nodes();
        analyze_table(m_db_connection, table.schema(), table.name());
    }

    m_cache->log_stats();
}

void middle_pgsql_t::after_ways()
{
    assert(m_middle_state == middle_state::way);
#ifndef NDEBUG
    m_middle_state = middle_state::relation;
#endif

    m_db_copy.sync();
    if (!m_options->append) {
        auto const &table = m_tables.ways();
        analyze_table(m_db_connection, table.schema(), table.name());
    }
}

void middle_pgsql_t::after_relations()
{
    assert(m_middle_state == middle_state::relation);
#ifndef NDEBUG
    m_middle_state = middle_state::done;
#endif

    m_db_copy.sync();
    if (!m_options->append) {
        auto const &table = m_tables.relations();
        analyze_table(m_db_connection, table.schema(), table.name());
    }

    if (m_store_options.with_attributes && !m_options->droptemp) {
        if (m_append) {
            update_users_table();
        } else {
            write_users_table();
        }
    }

    // release the copy thread and its database connection
    m_copy_thread->finish();
}

middle_query_pgsql_t::middle_query_pgsql_t(
    connection_params_t const &connection_params,
    std::shared_ptr<node_locations_t> cache,
    std::shared_ptr<node_persistent_cache> persistent_cache,
    middle_pgsql_options const &options)
: m_db_connection(connection_params, "middle.query"), m_cache(std::move(cache)),
  m_persistent_cache(std::move(persistent_cache)), m_store_options(options)
{
    // Disable JIT and parallel workers as they are known to cause
    // problems when accessing the intarrays.
    m_db_connection.set_config("jit_above_cost", "-1");
    m_db_connection.set_config("max_parallel_workers_per_gather", "0");
}

void middle_pgsql_t::start()
{
    assert(m_middle_state == middle_state::constructed);
#ifndef NDEBUG
    m_middle_state = middle_state::node;
#endif

    if (m_options->append) {
        // Disable JIT and parallel workers as they are known to cause
        // problems when accessing the intarrays.
        m_db_connection.set_config("jit_above_cost", "-1");
        m_db_connection.set_config("max_parallel_workers_per_gather", "0");

        // Remember the maximum OSM ids in the middle tables. This is a very
        // fast operation due to the index on the table. Later when we need
        // to delete entries, we don't have to bother with entries that are
        // definitely not in the table.
        if (m_store_options.nodes) {
            m_tables.nodes().init_max_id(m_db_connection);
        }
        m_tables.ways().init_max_id(m_db_connection);
        m_tables.relations().init_max_id(m_db_connection);
        return;
    }

    if (m_store_options.nodes) {
        log_debug("Setting up table 'nodes'");
        dbexec(R"(DROP TABLE IF EXISTS {schema}"{prefix}_nodes" CASCADE)");
        dbexec("CREATE {unlogged} TABLE {schema}\"{prefix}_nodes\" ("
               " id int8 PRIMARY KEY {using_tablespace},"
               " lat int4 NOT NULL,"
               " lon int4 NOT NULL,"
               "{attribute_columns_definition}"
               " tags jsonb"
               ") {data_tablespace}");
    }

    log_debug("Setting up table 'ways'");
    dbexec(R"(DROP TABLE IF EXISTS {schema}"{prefix}_ways" CASCADE)");
    dbexec("CREATE {unlogged} TABLE {schema}\"{prefix}_ways\" ("
           " id int8 PRIMARY KEY {using_tablespace},"
           "{attribute_columns_definition}"
           " nodes int8[] NOT NULL,"
           " tags jsonb"
           ") {data_tablespace}");

    log_debug("Setting up table 'rels'");
    dbexec(R"(DROP TABLE IF EXISTS {schema}"{prefix}_rels" CASCADE)");
    dbexec("CREATE {unlogged} TABLE {schema}\"{prefix}_rels\" ("
           " id int8 PRIMARY KEY {using_tablespace},"
           "{attribute_columns_definition}"
           " members jsonb NOT NULL,"
           " tags jsonb"
           ") {data_tablespace}");

    if (m_store_options.with_attributes) {
        log_debug("Setting up table 'users'");
        dbexec(R"(DROP TABLE IF EXISTS {schema}"{prefix}_users" CASCADE)");
        dbexec("CREATE TABLE {schema}\"{prefix}_users\" ("
               " id INT4 PRIMARY KEY {using_tablespace},"
               " name TEXT NOT NULL"
               ") {data_tablespace}");
    }
}

void middle_pgsql_t::write_users_table()
{
    auto const table_name = m_options->prefix + "_users";

    log_info("Writing {} entries to table '{}'...", m_users.size(), table_name);

    auto const users_table = std::make_shared<db_target_descr_t>(
        m_options->dbschema, table_name, "id");

    for (auto const &[id, name] : m_users) {
        m_db_copy.new_line(users_table);
        m_db_copy.add_columns(id, name);
        m_db_copy.finish_line();
    }
    m_db_copy.sync();

    m_users.clear();

    analyze_table(m_db_connection, m_options->dbschema, table_name);
}

void middle_pgsql_t::update_users_table()
{
    auto const table_name = m_options->prefix + "_users";

    log_info("Writing {} entries to table '{}'...", m_users.size(), table_name);

    m_db_connection.prepare(
        "insert_user",
        "INSERT INTO {}.\"{}\" (id, name) VALUES ($1::int8, $2::text)"
        " ON CONFLICT (id) DO UPDATE SET id=EXCLUDED.id",
        m_options->dbschema, table_name);

    for (auto const &[id, name] : m_users) {
        m_db_connection.exec_prepared("insert_user", id, name);
    }

    m_users.clear();

    analyze_table(m_db_connection, m_options->dbschema, table_name);
}

void middle_pgsql_t::build_way_node_index()
{
    dbexec("CREATE OR REPLACE FUNCTION"
           "    {schema}\"{prefix}_index_bucket\"(int8[])"
           "  RETURNS int8[] AS $$"
           "  SELECT ARRAY(SELECT DISTINCT"
           "    unnest($1) >> {way_node_index_id_shift})"
           "$$ LANGUAGE SQL IMMUTABLE");

    auto const create_ways_index =
        render_template("CREATE INDEX \"{prefix}_ways_nodes_bucket_idx\""
                        " ON {schema}\"{prefix}_ways\""
                        " USING GIN ({schema}\"{prefix}_index_bucket\"(nodes))"
                        " WITH (fastupdate = off) {index_tablespace}");

    log_info("Building index on middle ways table");
    m_tables.ways().task_set(thread_pool().submit([&, create_ways_index]() {
        pg_conn_t const db_connection{m_options->connection_params,
                                      "middle.index.ways"};
        db_connection.exec(create_ways_index);
    }));
}

void middle_pgsql_t::build_relation_member_indexes()
{
    dbexec("CREATE OR REPLACE FUNCTION"
           " {schema}\"{prefix}_member_ids\"(jsonb, char)"
           " RETURNS int8[] AS $$"
           "  SELECT array_agg((el->>'ref')::int8)"
           "   FROM jsonb_array_elements($1) AS el"
           "    WHERE el->>'type' = $2"
           "$$ LANGUAGE SQL IMMUTABLE");

    auto const create_rels_index_node_members = render_template(
        "CREATE INDEX \"{prefix}_rels_node_members_idx\""
        " ON {schema}\"{prefix}_rels\" USING GIN"
        " (({schema}\"{prefix}_member_ids\"(members, 'N'::char)))"
        " WITH (fastupdate = off) {index_tablespace}");

    auto const create_rels_index_way_members = render_template(
        "CREATE INDEX \"{prefix}_rels_way_members_idx\""
        " ON {schema}\"{prefix}_rels\" USING GIN"
        " (({schema}\"{prefix}_member_ids\"(members, 'W'::char)))"
        " WITH (fastupdate = off) {index_tablespace}");

    log_info("Building indexes on middle rels table");
    m_tables.relations().task_set(thread_pool().submit(
        [&, create_rels_index_node_members, create_rels_index_way_members]() {
            pg_conn_t const db_connection{m_options->connection_params,
                                          "middle.index.rels"};
            db_connection.exec(create_rels_index_node_members);
            db_connection.exec(create_rels_index_way_members);
        }));
}

void middle_pgsql_t::stop()
{
    assert(m_middle_state == middle_state::done);

    m_cache.reset();
    m_persistent_cache.reset();

    if (m_options->droptemp) {
        // Dropping the tables is fast, so do it synchronously to guarantee
        // that the space is freed before creating the other indices.
        for (auto const &table : m_tables) {
            table.drop_table(m_db_connection);
        }
    } else if (!m_options->append) {
        build_way_node_index();
        build_relation_member_indexes();
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

namespace {

void init_params(params_t *params, options_t const &options)
{
    std::string const schema = "\"" + options.middle_dbschema + "\".";

    params->set("prefix", options.prefix);
    params->set("schema", schema);
    params->set("unlogged", options.droptemp ? "UNLOGGED" : "");
    params->set("data_tablespace", tablespace_clause(options.tblsslim_data));
    params->set("index_tablespace", tablespace_clause(options.tblsslim_index));
    params->set("way_node_index_id_shift", 5);

    if (options.tblsslim_index.empty()) {
        params->set("using_tablespace", "");
    } else {
        params->set("using_tablespace",
                    "USING INDEX TABLESPACE " + options.tblsslim_index);
    }

    if (options.extra_attributes) {
        params->set("attribute_columns_definition",
                    " created timestamp with time zone,"
                    " version int4,"
                    " changeset_id int4,"
                    " user_id int4,");
        params->set("attribute_columns_use",
                    ", EXTRACT(EPOCH FROM created) AS created, version, "
                    "changeset_id, user_id, u.name");
        params->set("users_table_access", "LEFT JOIN " + schema + '"' +
                                              options.prefix +
                                              "_users\" u ON o.user_id = u.id");
    } else {
        params->set("attribute_columns_definition", "");
        params->set("attribute_columns_use", "");
        params->set("users_table_access", "");
    }
}

} // anonymous namespace

middle_pgsql_t::middle_pgsql_t(std::shared_ptr<thread_pool_t> thread_pool,
                               options_t const *options)
: middle_t(std::move(thread_pool)), m_options(options),
  m_cache(std::make_unique<node_locations_t>(
      static_cast<std::size_t>(options->cache) * 1024UL * 1024UL)),
  m_db_connection(m_options->connection_params, "middle.main"),
  m_copy_thread(std::make_shared<db_copy_thread_t>(options->connection_params)),
  m_db_copy(m_copy_thread), m_append(options->append)
{
    m_store_options.with_attributes = options->extra_attributes;

    if (options->middle_with_nodes) {
        m_store_options.nodes = true;
    }

    if (options->flat_node_file.empty()) {
        m_store_options.nodes = true;
        m_store_options.untagged_nodes = true;
    } else {
        m_store_options.use_flat_node_file = true;
        m_persistent_cache = std::make_shared<node_persistent_cache>(
            options->flat_node_file, !options->append, options->droptemp);
    }

    log_debug("Mid: pgsql, cache={}", options->cache);

    init_params(&m_params, *options);

    m_tables.nodes() = table_desc{*options, "nodes"};
    m_tables.ways() = table_desc{*options, "ways"};
    m_tables.relations() = table_desc{*options, "rels"};
}

void middle_pgsql_t::set_requirements(
    output_requirements const & /*requirements*/)
{
    log_debug("Middle 'pgsql' options:");
    log_debug("  nodes: {}", m_store_options.nodes);
    log_debug("  untagged_nodes: {}", m_store_options.untagged_nodes);
    log_debug("  use_flat_node_file: {}", m_store_options.use_flat_node_file);
    log_debug("  with_attributes: {}", m_store_options.with_attributes);
}

std::shared_ptr<middle_query_t>
middle_pgsql_t::get_query_instance()
{
    // NOTE: this is thread safe for use in pending async processing only
    // because during that process they are only read from
    auto mid = std::make_unique<middle_query_pgsql_t>(
        m_options->connection_params, m_cache, m_persistent_cache,
        m_store_options);

    if (m_store_options.nodes) {
        mid->prepare("get_node_location",
                     render_template(
                         "SELECT id, lon, lat FROM {schema}\"{prefix}_nodes\""
                         " WHERE id = $1::int8"));

        mid->prepare("get_node_list",
                     render_template(
                         "SELECT id, lon, lat FROM {schema}\"{prefix}_nodes\""
                         " WHERE id = ANY($1::int8[])"));

        mid->prepare(
            "get_node",
            render_template("SELECT lon, lat, tags{attribute_columns_use}"
                            " FROM {schema}\"{prefix}_nodes\" o"
                            " {users_table_access}"
                            " WHERE o.id = $1::int8"));
    }

    mid->prepare("get_way",
                 render_template("SELECT nodes, tags{attribute_columns_use}"
                                 " FROM {schema}\"{prefix}_ways\" o"
                                 " {users_table_access}"
                                 " WHERE o.id = $1::int8"));

    mid->prepare(
        "get_way_list",
        render_template("SELECT o.id, nodes, tags{attribute_columns_use}"
                        " FROM {schema}\"{prefix}_ways\" o"
                        "  {users_table_access}"
                        " WHERE o.id = ANY($1::int8[])"));

    mid->prepare("get_rel",
                 render_template("SELECT members, tags{attribute_columns_use}"
                                 " FROM {schema}\"{prefix}_rels\" o"
                                 " {users_table_access}"
                                 " WHERE o.id = $1::int8"));

    return std::shared_ptr<middle_query_t>(mid.release());
}
