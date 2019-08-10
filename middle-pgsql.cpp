/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#ifdef _WIN32
using namespace std;
#endif

#include <stdexcept>
#include <unordered_map>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>

#include <boost/format.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/builder/osm_object_builder.hpp>

#include <libpq-fe.h>

#include "middle-pgsql.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-pgsql.hpp"
#include "util.hpp"

/**
 * Helper to create SQL queries.
 *
 * The input string is mangled as follows:
 * %p replaced by the content of the "prefix" option
 * %i replaced by the content of the "tblsslim_data" option
 * %t replaced by the content of the "tblssslim_index" option
 * %m replaced by "UNLOGGED" if the "droptemp" option is set
 * other occurrences of the "%" char are treated normally.
 * any occurrence of { or } will be ignored (not copied to output string);
 * anything inside {} is only copied if it contained at least one of
 * %p, %i, %t, %m that was not NULL.
 *
 * So, the input string
 *    Hello{ dear %i}!
 * will, if i is set to "John", translate to
 *    Hello dear John!
 * but if i is unset, translate to
 *    Hello!
 *
 * This is used for constructing SQL queries with proper tablespace settings.
 */
static void set_prefix_and_tbls(options_t const *options, std::string *string)
{
    if (string->empty()) {
        return;
    }

    char buffer[1024];
    char *openbrace = nullptr;
    bool copied = false;
    char const *source = string->c_str();
    char *dest = buffer;

    while (*source) {
        if (*source == '{') {
            openbrace = dest;
            copied = false;
            source++;
            continue;
        } else if (*source == '}') {
            if (!copied && openbrace)
                dest = openbrace;
            source++;
            continue;
        } else if (*source == '%') {
            if (*(source + 1) == 'p') {
                if (!options->prefix.empty()) {
                    strcpy(dest, options->prefix.c_str());
                    dest += strlen(options->prefix.c_str());
                    copied = true;
                }
                source += 2;
                continue;
            } else if (*(source + 1) == 't') {
                if (options->tblsslim_data) {
                    strcpy(dest, options->tblsslim_data->c_str());
                    dest += strlen(options->tblsslim_data->c_str());
                    copied = true;
                }
                source += 2;
                continue;
            } else if (*(source + 1) == 'i') {
                if (options->tblsslim_index) {
                    strcpy(dest, options->tblsslim_index->c_str());
                    dest += strlen(options->tblsslim_index->c_str());
                    copied = true;
                }
                source += 2;
                continue;
            } else if (*(source + 1) == 'm') {
                if (options->droptemp) {
                    strcpy(dest, "UNLOGGED");
                    dest += 8;
                    copied = true;
                }
                source += 2;
                continue;
            }
        }
        *(dest++) = *(source++);
    }
    *dest = 0;

    string->assign(buffer);
}

middle_pgsql_t::table_desc::table_desc(options_t const *options,
                                       char const *name, char const *create,
                                       char const *prepare_query,
                                       char const *prepare_intarray,
                                       char const *array_indexes)
: m_create(create), m_prepare_query(prepare_query),
  m_prepare_intarray(prepare_intarray), m_array_indexes(array_indexes),
  m_copy_target(std::make_shared<db_target_descr_t>())
{
    m_copy_target->name = name;
    m_copy_target->id = "id"; // XXX hardcoded column name

    set_prefix_and_tbls(options, &m_copy_target->name);
    set_prefix_and_tbls(options, &m_create);
    set_prefix_and_tbls(options, &m_prepare_query);
    set_prefix_and_tbls(options, &m_prepare_intarray);
    set_prefix_and_tbls(options, &m_array_indexes);
}



pg_result_t middle_query_pgsql_t::exec_prepared(char const *stmt,
                                                char const *param) const
{
    return pgsql_execPrepared(m_sql_conn, stmt, 1, &param, PGRES_TUPLES_OK);
}

pg_result_t middle_query_pgsql_t::exec_prepared(char const *stmt,
                                                osmid_t osm_id) const
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%" PRIdOSMID, osm_id);
    return exec_prepared(stmt, buffer);
}

pg_result_t middle_pgsql_t::exec_prepared(char const *stmt, osmid_t osm_id) const
{
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%" PRIdOSMID, osm_id);
    auto bptr = const_cast<char const *>(buffer);
    return pgsql_execPrepared(m_query_conn, stmt, 1, &bptr, PGRES_TUPLES_OK);
}

void middle_query_pgsql_t::exec_sql(std::string const &sql_cmd) const
{
    pgsql_exec(m_sql_conn, PGRES_COMMAND_OK, "%s", sql_cmd.c_str());
}

void middle_pgsql_t::table_desc::stop(std::string conninfo, bool droptemp, bool build_indexes)
{
    time_t start, end;

    fprintf(stderr, "Stopping table: %s\n", name());
    time(&start);

    auto sql_conn = PQconnectdb(conninfo.c_str());

    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n",
                PQerrorMessage(sql_conn));
        util::exit_nicely();
    }

    if (droptemp) {
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE %s", name());
    } else if (build_indexes && !m_array_indexes.empty()) {
        fprintf(stderr, "Building index on table: %s\n", name());
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", m_array_indexes.c_str());
    }

    PQfinish(sql_conn);
    time(&end);

    fprintf(stderr, "Stopped table: %s in %is\n", name(), (int)(end - start));
}


namespace {
// Decodes a portion of an array literal from postgres */
// Argument should point to beginning of literal, on return points to delimiter */
inline const char *decode_upto( const char *src, char *dst )
{
  int quoted = (*src == '"');
  if( quoted ) src++;

  while( quoted ? (*src != '"') : (*src != ',' && *src != '}') )
  {
    if( *src == '\\' )
    {
      switch( src[1] )
      {
        case 'n': *dst++ = '\n'; break;
        case 't': *dst++ = '\t'; break;
        default: *dst++ = src[1]; break;
      }
      src+=2;
    }
    else
      *dst++ = *src++;
  }
  if( quoted ) src++;
  *dst = 0;
  return src;
}

template <typename T>
void pgsql_parse_tags(const char *string, osmium::memory::Buffer &buffer, T &obuilder)
{
    if( *string++ != '{' )
        return;

    char key[1024];
    char val[1024];
    osmium::builder::TagListBuilder builder(buffer, &obuilder);

    while( *string != '}' ) {
        string = decode_upto(string, key);
        // String points to the comma */
        string++;
        string = decode_upto(string, val);
        builder.add_tag(key, val);
        // String points to the comma or closing '}' */
        if( *string == ',' ) {
            string++;
        }
    }
}

void pgsql_parse_members(const char *string, osmium::memory::Buffer &buffer,
                         osmium::builder::RelationBuilder &obuilder)
{
    if( *string++ != '{' )
        return;

    char role[1024];
    osmium::builder::RelationMemberListBuilder builder(buffer, &obuilder);

    while( *string != '}' ) {
        char type = string[0];
        char *endp;
        osmid_t id = strtoosmid(string + 1, &endp, 10);
        // String points to the comma */
        string = decode_upto(endp + 1, role);
        builder.add_member(osmium::char_to_item_type(type), id, role);
        // String points to the comma or closing '}' */
        if( *string == ',' ) {
            string++;
        }
    }
}

void pgsql_parse_nodes(const char *string, osmium::memory::Buffer &buffer,
                         osmium::builder::WayBuilder &builder)
{
    if (*string++ == '{') {
        osmium::builder::WayNodeListBuilder wnl_builder(buffer, &builder);
        while (*string != '}') {
            char *ptr;
            wnl_builder.add_node_ref(strtoosmid(string, &ptr, 10));
            string = ptr;
            if (*string == ',') {
                string++;
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
    std::string buffer("{");

    // get nodes where possible from cache,
    // at the same time build a list for querying missing nodes from DB
    size_t pos = 0;
    for (auto &n : *nodes) {
        auto loc = m_cache->get(n.ref());
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
    auto res = exec_prepared("get_node_list", buffer.c_str());
    auto countPG = PQntuples(res.get());

    std::unordered_map<osmid_t, osmium::Location> locs;
    for (int i = 0; i < countPG; ++i) {
        locs.emplace(
            strtoosmid(PQgetvalue(res.get(), i, 0), nullptr, 10),
            osmium::Location(
                (int)strtol(PQgetvalue(res.get(), i, 2), nullptr, 10),
                (int)strtol(PQgetvalue(res.get(), i, 1), nullptr, 10)));
    }

    for (auto &n : *nodes) {
        auto el = locs.find(n.ref());
        if (el != locs.end()) {
            n.set_location(el->second);
            ++count;
        }

    }

    return count;
}

void middle_pgsql_t::nodes_set(osmium::Node const &node)
{
    cache->set(node.id(), node.location());

    if (out_options->flat_node_cache_enabled) {
        persistent_cache->set(node.id(), node.location());
    } else {
        m_db_copy.new_line(tables[NODE_TABLE].m_copy_target);

        m_db_copy.add_columns(node.id(), node.location().y(), node.location().x());

        m_db_copy.finish_line();
    }
}

size_t middle_query_pgsql_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    return m_persistent_cache ? m_persistent_cache->get_list(nodes)
                              : local_nodes_get_list(nodes);
}

void middle_pgsql_t::nodes_delete(osmid_t osm_id)
{
    if (out_options->flat_node_cache_enabled) {
        persistent_cache->set(osm_id, osmium::Location());
    } else {
        m_db_copy.new_line(tables[NODE_TABLE].m_copy_target);
        m_db_copy.delete_id(osm_id);
    }
}

void middle_pgsql_t::node_changed(osmid_t osm_id)
{
    if (!mark_pending) {
        return;
    }

    //keep track of whatever ways and rels these nodes intersect
    auto res = exec_prepared("mark_ways_by_node", osm_id);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }

    //do the rels too
    res = exec_prepared("mark_rels_by_node", osm_id);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
}

void middle_pgsql_t::ways_set(osmium::Way const &way)
{
    m_db_copy.new_line(tables[WAY_TABLE].m_copy_target);

    m_db_copy.add_column(way.id());

    // nodes
    m_db_copy.new_array();
    for (auto const &n : way.nodes()) {
        m_db_copy.add_array_elem(n.ref());
    }
    m_db_copy.finish_array();

    buffer_store_tags(way, out_options->extra_attributes);

    m_db_copy.finish_line();
}

bool middle_query_pgsql_t::ways_get(osmid_t id,
                                    osmium::memory::Buffer &buffer) const
{
    // Make sure we're out of copy mode
    auto res = exec_prepared("get_way", id);

    if (PQntuples(res.get()) != 1) {
        return false;
    }

    {
        osmium::builder::WayBuilder builder(buffer);
        builder.set_id(id);

        pgsql_parse_nodes(PQgetvalue(res.get(), 0, 0), buffer, builder);
        pgsql_parse_tags(PQgetvalue(res.get(), 0, 1), buffer, builder);
    }

    buffer.commit();

    return true;
}

size_t
middle_query_pgsql_t::rel_way_members_get(osmium::Relation const &rel,
                                          rolelist_t *roles,
                                          osmium::memory::Buffer &buffer) const
{
    char tmp[16];

    // create a list of ids in tmp2 to query the database
    std::string tmp2("{");
    for (auto const &m : rel.members()) {
        if (m.type() == osmium::item_type::way) {
            snprintf(tmp, sizeof(tmp), "%" PRIdOSMID ",", m.ref());
            tmp2.append(tmp);
        }
    }

    if (tmp2.length() == 1) {
        return 0; // no ways found
    }
    // replace last , with } to complete list of ids
    tmp2[tmp2.length() - 1] = '}';

    // Make sures all ways have been written back.
    auto res = exec_prepared("get_way_list", tmp2.c_str());
    int countPG = PQntuples(res.get());

    idlist_t wayidspg;

    for (int i = 0; i < countPG; i++) {
        wayidspg.push_back(
            strtoosmid(PQgetvalue(res.get(), i, 0), nullptr, 10));
    }

    // Match the list of ways coming from postgres in a different order
    //   back to the list of ways given by the caller */
    size_t outres = 0;
    for (auto const &m : rel.members()) {
        if (m.type() != osmium::item_type::way) {
            continue;
        }
        for (int j = 0; j < countPG; j++) {
            if (m.ref() == wayidspg[j]) {
                {
                    osmium::builder::WayBuilder builder(buffer);
                    builder.set_id(m.ref());

                    pgsql_parse_nodes(PQgetvalue(res.get(), j, 1), buffer,
                                      builder);
                    pgsql_parse_tags(PQgetvalue(res.get(), j, 2), buffer,
                                     builder);
                }

                buffer.commit();
                if (roles) {
                    roles->emplace_back(m.role());
                }
                outres++;
                break;
            }
        }
    }

    return outres;
}


void middle_pgsql_t::ways_delete(osmid_t osm_id)
{
    m_db_copy.new_line(tables[WAY_TABLE].m_copy_target);
    m_db_copy.delete_id(osm_id);
}

void middle_pgsql_t::iterate_ways(middle_t::pending_processor& pf)
{
    // enqueue the jobs
    osmid_t id;
    while(id_tracker::is_valid(id = ways_pending_tracker->pop_mark()))
    {
        pf.enqueue_ways(id);
    }
    // in case we had higher ones than the middle
    pf.enqueue_ways(id_tracker::max());

    //let the threads work on them
    pf.process_ways();
}

void middle_pgsql_t::way_changed(osmid_t osm_id)
{
    //keep track of whatever rels this way intersects
    auto res = exec_prepared("mark_rels_by_way", osm_id);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
}

void middle_pgsql_t::relations_set(osmium::Relation const &rel)
{
    // Sort relation members by their type.
    idlist_t parts[3];

    for (auto const &m : rel.members()) {
        parts[osmium::item_type_to_nwr_index(m.type())].push_back(m.ref());
    }

    m_db_copy.new_line(tables[REL_TABLE].m_copy_target);

    // id, way offset, relation offset
    m_db_copy.add_columns(rel.id(), parts[0].size(),
                          parts[0].size() + parts[1].size());

    // parts
    m_db_copy.new_array();
    for (int i = 0; i < 3; ++i) {
        for (auto it : parts[i]) {
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
    buffer_store_tags(rel, out_options->extra_attributes);

    m_db_copy.finish_line();
}

bool middle_query_pgsql_t::relations_get(osmid_t id,
                                         osmium::memory::Buffer &buffer) const
{
    // Make sure relation table is out of copy mode
    auto res = exec_prepared("get_rel", id);
    // Fields are: members, tags, member_count */
    //
    if (PQntuples(res.get()) != 1) {
        return false;
    }

    {
        osmium::builder::RelationBuilder builder(buffer);
        builder.set_id(id);

        pgsql_parse_members(PQgetvalue(res.get(), 0, 0), buffer, builder);
        pgsql_parse_tags(PQgetvalue(res.get(), 0, 1), buffer, builder);
    }

    buffer.commit();

    return true;
}

void middle_pgsql_t::relations_delete(osmid_t osm_id)
{
    //keep track of whatever ways this relation interesects
    auto res = exec_prepared("mark_ways_by_rel", osm_id);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }

    m_db_copy.new_line(tables[REL_TABLE].m_copy_target);
    m_db_copy.delete_id(osm_id);
}

void middle_pgsql_t::iterate_relations(pending_processor& pf)
{
    // enqueue the jobs
    osmid_t id;
    while(id_tracker::is_valid(id = rels_pending_tracker->pop_mark()))
    {
        pf.enqueue_relations(id);
    }
    // in case we had higher ones than the middle
    pf.enqueue_relations(id_tracker::max());

    //let the threads work on them
    pf.process_relations();
}

void middle_pgsql_t::relation_changed(osmid_t osm_id)
{
    //keep track of whatever ways and rels these nodes intersect
    //TODO: dont need to stop the copy above since we are only reading?
    //TODO: can we just mark the id without querying? the where clause seems intersect reltable.parts with the id
    auto res = exec_prepared("mark_rels", osm_id);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
}

idlist_t middle_query_pgsql_t::relations_using_way(osmid_t way_id) const
{
    // Make sure relation table is out of copy mode */
    auto result = exec_prepared("rels_using_way", way_id);
    const int ntuples = PQntuples(result.get());
    idlist_t rel_ids;
    rel_ids.resize((size_t) ntuples);
    for (int i = 0; i < ntuples; ++i) {
        rel_ids[i] = strtoosmid(PQgetvalue(result.get(), i, 0), nullptr, 10);
    }

    return rel_ids;
}

void middle_pgsql_t::analyze()
{
    for (auto &t : tables) {
        pgsql_exec(m_query_conn, PGRES_COMMAND_OK, "ANALYZE %s", t.name());
    }
}

middle_query_pgsql_t::middle_query_pgsql_t(
    char const *conninfo, std::shared_ptr<node_ram_cache> const &cache,
    std::shared_ptr<node_persistent_cache> const &persistent_cache)
: m_sql_conn(PQconnectdb(conninfo)), m_cache(cache),
  m_persistent_cache(persistent_cache)
{
    // Check to see that the backend connection was successfully made, and if not, exit */
    if (PQstatus(m_sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n",
                PQerrorMessage(m_sql_conn));
        util::exit_nicely();
    }
}

middle_query_pgsql_t::~middle_query_pgsql_t()
{
    if (m_sql_conn) {
        PQfinish(m_sql_conn);
    }
}

void middle_pgsql_t::start()
{
    ways_pending_tracker.reset(new id_tracker());
    rels_pending_tracker.reset(new id_tracker());

    // Gazetter doesn't use mark-pending processing and consequently
    // needs no way-node index.
    // TODO Currently, set here to keep the impact on the code small.
    // We actually should have the output plugins report their needs
    // and pass that via the constructor to middle_t, so that middle_t
    // itself doesn't need to know about details of the output.
    if (out_options->output_backend == "gazetteer") {
        tables[WAY_TABLE].clear_array_indexes();
        mark_pending = false;
    }

    m_query_conn = PQconnectdb(out_options->database_options.conninfo().c_str());
    if (PQstatus(m_query_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n",
                PQerrorMessage(m_query_conn));
        util::exit_nicely();
    }

    if (append) {
        // Prepare queries for updating dependent objects
        for (auto &table : tables) {
            if (!table.m_prepare_intarray.empty()) {
                pgsql_exec(m_query_conn, PGRES_COMMAND_OK, "%s",
                           table.m_prepare_intarray.c_str());
            }
        }
    } else {
        // (Re)create tables.
        pgsql_exec(m_query_conn, PGRES_COMMAND_OK, "SET client_min_messages = WARNING");
        for (auto &table : tables) {
            fprintf(stderr, "Setting up table: %s\n", table.name());
            pgsql_exec(m_query_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s CASCADE", table.name());
            pgsql_exec(m_query_conn, PGRES_COMMAND_OK, "%s", table.m_create.c_str());
        }

        PQfinish(m_query_conn);
        m_query_conn = nullptr;
    }
}

void middle_pgsql_t::commit()
{
    m_db_copy.sync();
    // release the copy thread and its query connection
    m_copy_thread->finish();

    if (m_query_conn) {
        PQfinish(m_query_conn);
        m_query_conn = nullptr;
    }
}

void middle_pgsql_t::flush(osmium::item_type)
{
    m_db_copy.sync();
}

void middle_pgsql_t::stop(osmium::thread::Pool &pool)
{
    cache.reset();
    if (out_options->flat_node_cache_enabled) {
        persistent_cache.reset();
    }

    if (out_options->droptemp) {
        // Dropping the tables is fast, so do it synchronously to guarantee
        // that the space is freed before creating the other indices.
        for (auto &t : tables) {
            t.stop(out_options->database_options.conninfo(),
                   out_options->droptemp, !append);
        }
    } else {
        for (auto &t : tables) {
            pool.submit(std::bind(&middle_pgsql_t::table_desc::stop,
                                  &t, out_options->database_options.conninfo(), out_options->droptemp, !append));
        }
    }
}

middle_pgsql_t::middle_pgsql_t(options_t const *options)
: append(options->append), mark_pending(true), out_options(options),
  cache(new node_ram_cache(options->alloc_chunkwise | ALLOC_LOSSY,
                           options->cache)),
  m_query_conn(nullptr), m_copy_thread(std::make_shared<db_copy_thread_t>(
                             options->database_options.conninfo())),
  m_db_copy(m_copy_thread)
{
    if (options->flat_node_cache_enabled) {
        persistent_cache.reset(new node_persistent_cache(options, cache));
    }

    fprintf(stderr, "Mid: pgsql, cache=%d\n", options->cache);

    // clang-format off
    /*table = t_node,*/
    tables[NODE_TABLE] = table_desc(options,
            /*name*/ "%p_nodes",
          /*create*/ "CREATE %m TABLE %p_nodes (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, lat int4 not null, lon int4 not null) {TABLESPACE %t};\n",
         /*prepare_query */
               "PREPARE get_node_list(" POSTGRES_OSMID_TYPE "[]) AS SELECT id, lat, lon FROM %p_nodes WHERE id = ANY($1::" POSTGRES_OSMID_TYPE "[]);\n"
                         );
    tables[WAY_TABLE] = table_desc(options,
        /*table t_way,*/
            /*name*/ "%p_ways",
          /*create*/ "CREATE %m TABLE %p_ways (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, nodes " POSTGRES_OSMID_TYPE "[] not null, tags text[]) {TABLESPACE %t};\n",
         /*prepare_query */
               "PREPARE get_way (" POSTGRES_OSMID_TYPE ") AS SELECT nodes, tags, array_upper(nodes,1) FROM %p_ways WHERE id = $1;\n"
               "PREPARE get_way_list (" POSTGRES_OSMID_TYPE "[]) AS SELECT id, nodes, tags, array_upper(nodes,1) FROM %p_ways WHERE id = ANY($1::" POSTGRES_OSMID_TYPE "[]);\n",
/*prepare_intarray*/
               "PREPARE mark_ways_by_node(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE nodes && ARRAY[$1];\n"
               "PREPARE mark_ways_by_rel(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE id IN (SELECT unnest(parts[way_off+1:rel_off]) FROM %p_rels WHERE id = $1);\n",

   /*array_indexes*/ "CREATE INDEX %p_ways_nodes ON %p_ways USING gin (nodes) WITH (FASTUPDATE=OFF) {TABLESPACE %i};\n"
                         );
    tables[REL_TABLE] = table_desc(options, 
        /*table = t_rel,*/
            /*name*/ "%p_rels",
          /*create*/ "CREATE %m TABLE %p_rels(id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, way_off int2, rel_off int2, parts " POSTGRES_OSMID_TYPE "[], members text[], tags text[]) {TABLESPACE %t};\n",
         /*prepare_query */
               "PREPARE get_rel (" POSTGRES_OSMID_TYPE ") AS SELECT members, tags, array_upper(members,1)/2 FROM %p_rels WHERE id = $1;\n"
                "PREPARE rels_using_way(" POSTGRES_OSMID_TYPE ") AS SELECT id FROM %p_rels WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1];\n",
/*prepare_intarray*/
                "PREPARE mark_rels_by_node(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE nodes && ARRAY[$1];\n"
                "PREPARE mark_rels_by_way(" POSTGRES_OSMID_TYPE ") AS select id from %p_rels WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1];\n"
                "PREPARE mark_rels(" POSTGRES_OSMID_TYPE ") AS select id from %p_rels WHERE parts && ARRAY[$1] AND parts[rel_off+1:array_length(parts,1)] && ARRAY[$1];\n",

   /*array_indexes*/ "CREATE INDEX %p_rels_parts ON %p_rels USING gin (parts) WITH (FASTUPDATE=OFF) {TABLESPACE %i};\n"
                         );
    // clang-format on
}

std::shared_ptr<middle_query_t>
middle_pgsql_t::get_query_instance(std::shared_ptr<middle_t> const &from) const
{
    auto *src = dynamic_cast<middle_pgsql_t *>(from.get());
    assert(src);

    // NOTE: this is thread safe for use in pending async processing only because
    // during that process they are only read from
    std::unique_ptr<middle_query_pgsql_t> mid(new middle_query_pgsql_t(
        src->out_options->database_options.conninfo().c_str(), src->cache,
        src->persistent_cache));

    // We use a connection per table to enable the use of COPY
    for (int i = 0; i < NUM_TABLES; i++) {
        mid->exec_sql(src->tables[i].m_prepare_query);
    }

    return std::shared_ptr<middle_query_t>(mid.release());
}

size_t middle_pgsql_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}
