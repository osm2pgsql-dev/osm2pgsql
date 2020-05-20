/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <stdexcept>
#include <unordered_map>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/types_from_string.hpp>

#include "format.hpp"
#include "middle-pgsql.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-pgsql.hpp"
#include "util.hpp"

/**
 * Iterate over the result from a pgsql query and call the func with all
 * the ids from the first column.
 *
 * \param result The result to iterate over.
 * \param func Lambda taking an osmid_t as only parameter.
 */
template <typename FUNC>
void for_each_id(pg_result_t const &result, FUNC &&func)
{
    for (int i = 0; i < result.num_tuples(); ++i) {
        auto const id = osmium::string_to_object_id(result.get_value(i, 0));
        std::forward<FUNC>(func)(id);
    }
}

static std::string build_sql(options_t const &options, char const *templ)
{
    std::string const using_tablespace{options.tblsslim_index.empty()
                                           ? ""
                                           : "USING INDEX TABLESPACE " +
                                                 options.tblsslim_index};
    return fmt::format(
        templ, fmt::arg("prefix", options.prefix),
        fmt::arg("unlogged", options.droptemp ? "UNLOGGED" : ""),
        fmt::arg("using_tablespace", using_tablespace),
        fmt::arg("data_tablespace", tablespace_clause(options.tblsslim_data)),
        fmt::arg("index_tablespace",
                 tablespace_clause(options.tblsslim_index)));
}

middle_pgsql_t::table_desc::table_desc(options_t const &options,
                                       table_sql const &ts)
: m_create(build_sql(options, ts.create_table)),
  m_prepare_query(build_sql(options, ts.prepare_query)),
  m_prepare_intarray(build_sql(options, ts.prepare_mark)),
  m_array_indexes(build_sql(options, ts.create_index)),
  m_copy_target(std::make_shared<db_target_descr_t>())
{
    m_copy_target->name = build_sql(options, ts.name);
    m_copy_target->id = "id"; // XXX hardcoded column name
}

void middle_query_pgsql_t::exec_sql(std::string const &sql_cmd) const
{
    m_sql_conn.exec(sql_cmd);
}

void middle_pgsql_t::table_desc::stop(std::string const &conninfo,
                                      bool droptemp, bool build_indexes)
{
    fmt::print(stderr, "Stopping table: {}\n", name());
    util::timer_t timer;

    // Use a temporary connection here because we might run in a separate
    // thread context.
    pg_conn_t sql_conn{conninfo};

    if (droptemp) {
        sql_conn.exec("DROP TABLE {}"_format(name()));
    } else if (build_indexes && !m_array_indexes.empty()) {
        fmt::print(stderr, "Building index on table: {}\n", name());
        sql_conn.exec(m_array_indexes);
    }

    fmt::print(stderr, "Stopped table: {} in {}s\n", name(), timer.stop());
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
void pgsql_parse_tags(char const *string, osmium::memory::Buffer &buffer,
                      T &obuilder)
{
    if (*string++ != '{') {
        return;
    }

    char key[1024];
    char val[1024];
    osmium::builder::TagListBuilder builder{buffer, &obuilder};

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

void pgsql_parse_members(char const *string, osmium::memory::Buffer &buffer,
                         osmium::builder::RelationBuilder &obuilder)
{
    if (*string++ != '{') {
        return;
    }

    char role[1024];
    osmium::builder::RelationMemberListBuilder builder{buffer, &obuilder};

    while (*string != '}') {
        char type = string[0];
        char *endp;
        osmid_t id = strtoosmid(string + 1, &endp, 10);
        // String points to the comma */
        string = decode_upto(endp + 1, role);
        builder.add_member(osmium::char_to_item_type(type), id, role);
        // String points to the comma or closing '}' */
        if (*string == ',') {
            ++string;
        }
    }
}

void pgsql_parse_nodes(char const *string, osmium::memory::Buffer &buffer,
                       osmium::builder::WayBuilder &builder)
{
    if (*string++ == '{') {
        osmium::builder::WayNodeListBuilder wnl_builder{buffer, &builder};
        while (*string != '}') {
            char *ptr;
            wnl_builder.add_node_ref(strtoosmid(string, &ptr, 10));
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

size_t
middle_query_pgsql_t::local_nodes_get_list(osmium::WayNodeList *nodes) const
{
    size_t count = 0;
    std::string buffer{"{"};

    // get nodes where possible from cache,
    // at the same time build a list for querying missing nodes from DB
    size_t pos = 0;
    for (auto &n : *nodes) {
        auto const loc = m_cache->get(n.ref());
        if (loc.valid()) {
            n.set_location(loc);
            ++count;
        } else {
            buffer += std::to_string(n.ref());
            buffer += ',';
        }
        ++pos;
    }

    if (count == pos) {
        return count; // all ids found in cache, nothing more to do
    }

    // get any remaining nodes from the DB
    buffer[buffer.size() - 1] = '}';

    // Nodes must have been written back at this point.
    auto const res = m_sql_conn.exec_prepared("get_node_list", buffer);
    std::unordered_map<osmid_t, osmium::Location> locs;
    for (int i = 0; i < res.num_tuples(); ++i) {
        locs.emplace(
            osmium::string_to_object_id(res.get_value(i, 0)),
            osmium::Location{(int)strtol(res.get_value(i, 2), nullptr, 10),
                             (int)strtol(res.get_value(i, 1), nullptr, 10)});
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

void middle_pgsql_t::node_set(osmium::Node const &node)
{
    m_cache->set(node.id(), node.location());

    if (m_out_options->flat_node_cache_enabled) {
        m_persistent_cache->set(node.id(), node.location());
    } else {
        m_db_copy.new_line(m_tables[NODE_TABLE].m_copy_target);

        m_db_copy.add_columns(node.id(), node.location().y(),
                              node.location().x());

        m_db_copy.finish_line();
    }
}

size_t middle_query_pgsql_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    return m_persistent_cache ? m_persistent_cache->get_list(nodes)
                              : local_nodes_get_list(nodes);
}

void middle_pgsql_t::node_delete(osmid_t osm_id)
{
    assert(m_append);

    if (m_out_options->flat_node_cache_enabled) {
        m_persistent_cache->set(osm_id, osmium::Location{});
    } else {
        m_db_copy.new_line(m_tables[NODE_TABLE].m_copy_target);
        m_db_copy.delete_object(osm_id);
    }
}

idlist_t middle_pgsql_t::get_ids(const char* stmt, osmid_t osm_id)
{
    idlist_t ids;

    auto const res = m_db_connection.exec_prepared(stmt, osm_id);
    ids.reserve(res.num_tuples());
    for_each_id(res, [&ids](osmid_t id) { ids.push_back(id); });

    return ids;
}

idlist_t middle_pgsql_t::get_ways_by_node(osmid_t osm_id)
{
    return get_ids("mark_ways_by_node", osm_id);
}

idlist_t middle_pgsql_t::get_rels_by_node(osmid_t osm_id)
{
    return get_ids("mark_rels_by_node", osm_id);
}

idlist_t middle_pgsql_t::get_rels_by_way(osmid_t osm_id)
{
    return get_ids("mark_rels_by_way", osm_id);
}

idlist_t middle_pgsql_t::get_rels_by_rel(osmid_t osm_id)
{
    return get_ids("mark_rels", osm_id);
}

idlist_t middle_pgsql_t::get_ways_by_rel(osmid_t osm_id)
{
    return get_ids("mark_ways_by_rel", osm_id);
}

void middle_pgsql_t::way_set(osmium::Way const &way)
{
    m_db_copy.new_line(m_tables[WAY_TABLE].m_copy_target);

    m_db_copy.add_column(way.id());

    // nodes
    m_db_copy.new_array();
    for (auto const &n : way.nodes()) {
        m_db_copy.add_array_elem(n.ref());
    }
    m_db_copy.finish_array();

    buffer_store_tags(way, m_out_options->extra_attributes);

    m_db_copy.finish_line();
}

bool middle_query_pgsql_t::way_get(osmid_t id,
                                   osmium::memory::Buffer &buffer) const
{
    auto const res = m_sql_conn.exec_prepared("get_way", id);

    if (res.num_tuples() != 1) {
        return false;
    }

    {
        osmium::builder::WayBuilder builder{buffer};
        builder.set_id(id);

        pgsql_parse_nodes(res.get_value(0, 0), buffer, builder);
        pgsql_parse_tags(res.get_value(0, 1), buffer, builder);
    }

    buffer.commit();

    return true;
}

size_t
middle_query_pgsql_t::rel_way_members_get(osmium::Relation const &rel,
                                          rolelist_t *roles,
                                          osmium::memory::Buffer &buffer) const
{
    // create a list of ids in id_list to query the database
    std::string id_list{"{"};
    for (auto const &m : rel.members()) {
        if (m.type() == osmium::item_type::way) {
            fmt::format_to(std::back_inserter(id_list), "{},", m.ref());
        }
    }

    if (id_list.size() == 1) {
        return 0; // no ways found
    }
    // replace last , with } to complete list of ids
    id_list.back() = '}';

    auto const res = m_sql_conn.exec_prepared("get_way_list", id_list);
    idlist_t wayidspg;
    for_each_id(res, [&wayidspg](osmid_t id) {
        wayidspg.push_back(id);
    });

    // Match the list of ways coming from postgres in a different order
    //   back to the list of ways given by the caller */
    size_t outres = 0;
    for (auto const &m : rel.members()) {
        if (m.type() != osmium::item_type::way) {
            continue;
        }
        for (int j = 0; j < res.num_tuples(); ++j) {
            if (m.ref() == wayidspg[j]) {
                {
                    osmium::builder::WayBuilder builder{buffer};
                    builder.set_id(m.ref());

                    pgsql_parse_nodes(res.get_value(j, 1), buffer, builder);
                    pgsql_parse_tags(res.get_value(j, 2), buffer, builder);
                }

                buffer.commit();
                if (roles) {
                    roles->emplace_back(m.role());
                }
                ++outres;
                break;
            }
        }
    }

    return outres;
}

void middle_pgsql_t::way_delete(osmid_t osm_id)
{
    assert(m_append);
    m_db_copy.new_line(m_tables[WAY_TABLE].m_copy_target);
    m_db_copy.delete_object(osm_id);
}

void middle_pgsql_t::relation_set(osmium::Relation const &rel)
{
    // Sort relation members by their type.
    idlist_t parts[3];

    for (auto const &m : rel.members()) {
        parts[osmium::item_type_to_nwr_index(m.type())].push_back(m.ref());
    }

    m_db_copy.new_line(m_tables[REL_TABLE].m_copy_target);

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
    buffer_store_tags(rel, m_out_options->extra_attributes);

    m_db_copy.finish_line();
}

bool middle_query_pgsql_t::relation_get(osmid_t id,
                                        osmium::memory::Buffer &buffer) const
{
    auto const res = m_sql_conn.exec_prepared("get_rel", id);
    // Fields are: members, tags, member_count */
    //
    if (res.num_tuples() != 1) {
        return false;
    }

    {
        osmium::builder::RelationBuilder builder{buffer};
        builder.set_id(id);

        pgsql_parse_members(res.get_value(0, 0), buffer, builder);
        pgsql_parse_tags(res.get_value(0, 1), buffer, builder);
    }

    buffer.commit();

    return true;
}

void middle_pgsql_t::relation_delete(osmid_t osm_id)
{
    assert(m_append);

    m_db_copy.new_line(m_tables[REL_TABLE].m_copy_target);
    m_db_copy.delete_object(osm_id);
}

idlist_t middle_query_pgsql_t::relations_using_way(osmid_t way_id) const
{
    auto const result = m_sql_conn.exec_prepared("rels_using_way", way_id);
    idlist_t rel_ids;
    rel_ids.reserve((size_t)result.num_tuples());
    for_each_id(result, [&rel_ids](osmid_t id) { rel_ids.push_back(id); });

    return rel_ids;
}

void middle_pgsql_t::analyze()
{
    for (auto const &table : m_tables) {
        m_db_connection.exec("ANALYZE {}"_format(table.name()));
    }
}

middle_query_pgsql_t::middle_query_pgsql_t(
    std::string const &conninfo, std::shared_ptr<node_ram_cache> const &cache,
    std::shared_ptr<node_persistent_cache> const &persistent_cache)
: m_sql_conn(conninfo), m_cache(cache), m_persistent_cache(persistent_cache)
{}

void middle_pgsql_t::start()
{
    // Gazetter doesn't use mark-pending processing and consequently
    // needs no way-node index.
    // TODO Currently, set here to keep the impact on the code small.
    // We actually should have the output plugins report their needs
    // and pass that via the constructor to middle_t, so that middle_t
    // itself doesn't need to know about details of the output.
    if (m_out_options->output_backend == "gazetteer") {
        m_tables[WAY_TABLE].clear_array_indexes();
        m_mark_pending = false;
    }

    if (m_append) {
        // Prepare queries for updating dependent objects
        for (auto &table : m_tables) {
            if (!table.m_prepare_intarray.empty()) {
                m_db_connection.exec(table.m_prepare_intarray);
            }
        }
    } else {
        // (Re)create tables.
        m_db_connection.exec("SET client_min_messages = WARNING");
        for (auto &table : m_tables) {
            fmt::print(stderr, "Setting up table: {}\n", table.name());
            m_db_connection.exec(
                "DROP TABLE IF EXISTS {} CASCADE"_format(table.name()));
            m_db_connection.exec(table.m_create);
        }

        // The extra query connection is only needed in append mode, so close.
        m_db_connection.close();
    }
}

void middle_pgsql_t::commit()
{
    m_db_copy.sync();
    // release the copy thread and its query connection
    m_copy_thread->finish();

    m_db_connection.close();
}

void middle_pgsql_t::flush() { m_db_copy.sync(); }

void middle_pgsql_t::stop(osmium::thread::Pool &pool)
{
    m_cache.reset();
    if (m_out_options->flat_node_cache_enabled) {
        m_persistent_cache.reset();
    }

    if (m_out_options->droptemp) {
        // Dropping the tables is fast, so do it synchronously to guarantee
        // that the space is freed before creating the other indices.
        for (auto &table : m_tables) {
            table.stop(m_out_options->database_options.conninfo(),
                       m_out_options->droptemp, !m_append);
        }
    } else {
        for (auto &table : m_tables) {
            pool.submit(std::bind(&middle_pgsql_t::table_desc::stop, &table,
                                  m_out_options->database_options.conninfo(),
                                  m_out_options->droptemp, !m_append));
        }
    }
}

static table_sql sql_for_nodes() noexcept
{
    table_sql sql{};

    sql.name = "{prefix}_nodes";

    sql.create_table = "CREATE {unlogged} TABLE {prefix}_nodes ("
                       "  id int8 PRIMARY KEY {using_tablespace},"
                       "  lat int4 NOT NULL,"
                       "  lon int4 NOT NULL"
                       ") {data_tablespace};\n";

    sql.prepare_query = "PREPARE get_node_list(int8[]) AS"
                        "  SELECT id, lat, lon FROM {prefix}_nodes"
                        "  WHERE id = ANY($1::int8[]);\n";

    return sql;
}

static table_sql sql_for_ways() noexcept
{
    table_sql sql{};

    sql.name = "{prefix}_ways";

    sql.create_table = "CREATE {unlogged} TABLE {prefix}_ways ("
                       "  id int8 PRIMARY KEY {using_tablespace},"
                       "  nodes int8[] NOT NULL,"
                       "  tags text[]"
                       ") {data_tablespace};\n";

    sql.prepare_query = "PREPARE get_way(int8) AS"
                        "  SELECT nodes, tags, array_upper(nodes, 1)"
                        "    FROM {prefix}_ways WHERE id = $1;\n"
                        "PREPARE get_way_list(int8[]) AS"
                        "  SELECT id, nodes, tags, array_upper(nodes, 1)"
                        "    FROM {prefix}_ways WHERE id = ANY($1::int8[]);\n";

    sql.prepare_mark = "PREPARE mark_ways_by_node(int8) AS"
                       "  SELECT id FROM {prefix}_ways"
                       "    WHERE nodes && ARRAY[$1];\n"
                       "PREPARE mark_ways_by_rel(int8) AS"
                       "  SELECT id FROM {prefix}_ways"
                       "    WHERE id IN ("
                       "      SELECT unnest(parts[way_off+1:rel_off])"
                       "        FROM {prefix}_rels WHERE id = $1"
                       "    );\n";

    sql.create_index = "CREATE INDEX ON {prefix}_ways USING GIN (nodes)"
                       "  WITH (fastupdate = off) {index_tablespace};\n";

    return sql;
}

static table_sql sql_for_relations() noexcept
{
    table_sql sql{};

    sql.name = "{prefix}_rels";

    sql.create_table = "CREATE {unlogged} TABLE {prefix}_rels ("
                       "  id int8 PRIMARY KEY {using_tablespace},"
                       "  way_off int2,"
                       "  rel_off int2,"
                       "  parts int8[],"
                       "  members text[],"
                       "  tags text[]"
                       ") {data_tablespace};\n";

    sql.prepare_query = "PREPARE get_rel(int8) AS"
                        "  SELECT members, tags, array_upper(members, 1) / 2"
                        "    FROM {prefix}_rels WHERE id = $1;\n"
                        "PREPARE rels_using_way(int8) AS"
                        "  SELECT id FROM {prefix}_rels"
                        "    WHERE parts && ARRAY[$1]"
                        "      AND parts[way_off+1:rel_off] && ARRAY[$1];\n";

    sql.prepare_mark = "PREPARE mark_rels_by_node(int8) AS"
                       "  SELECT id FROM {prefix}_rels"
                       "    WHERE parts && ARRAY[$1]"
                       "      AND parts[1:way_off] && ARRAY[$1];\n"
                       "PREPARE mark_rels_by_way(int8) AS"
                       "  SELECT id FROM {prefix}_rels"
                       "    WHERE parts && ARRAY[$1]"
                       "      AND parts[way_off+1:rel_off] && ARRAY[$1];\n"
                       "PREPARE mark_rels(int8) AS"
                       "  SELECT id FROM {prefix}_rels"
                       "    WHERE parts && ARRAY[$1]"
                       "      AND parts[rel_off+1:array_length(parts,1)]"
                       "        && ARRAY[$1];\n";

    sql.create_index = "CREATE INDEX ON {prefix}_rels USING GIN (parts)"
                       "  WITH (fastupdate = off) {index_tablespace};\n";

    return sql;
}

middle_pgsql_t::middle_pgsql_t(options_t const *options)
: m_append(options->append), m_mark_pending(true), m_out_options(options),
  m_cache(new node_ram_cache{options->alloc_chunkwise | ALLOC_LOSSY,
                             options->cache}),
  m_db_connection(m_out_options->database_options.conninfo()),
  m_copy_thread(
      std::make_shared<db_copy_thread_t>(options->database_options.conninfo())),
  m_db_copy(m_copy_thread)
{
    if (options->flat_node_cache_enabled) {
        m_persistent_cache.reset(new node_persistent_cache{options, m_cache});
    }

    fmt::print(stderr, "Mid: pgsql, cache={}\n", options->cache);

    m_tables[NODE_TABLE] = table_desc{*options, sql_for_nodes()};
    m_tables[WAY_TABLE] = table_desc{*options, sql_for_ways()};
    m_tables[REL_TABLE] = table_desc{*options, sql_for_relations()};
}

std::shared_ptr<middle_query_t>
middle_pgsql_t::get_query_instance()
{
    // NOTE: this is thread safe for use in pending async processing only because
    // during that process they are only read from
    std::unique_ptr<middle_query_pgsql_t> mid{
        new middle_query_pgsql_t{m_out_options->database_options.conninfo(),
                                 m_cache, m_persistent_cache}};

    // We use a connection per table to enable the use of COPY
    for (auto &table : m_tables) {
        mid->exec_sql(table.m_prepare_query);
    }

    return std::shared_ptr<middle_query_t>(mid.release());
}
