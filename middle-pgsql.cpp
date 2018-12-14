/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include "config.h"

#ifdef _WIN32
using namespace std;
#endif

#ifdef _MSC_VER
#define alloca _alloca
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
#include "pgsql.hpp"
#include "util.hpp"

/**
 * Helper to create SQL queries.
 *
 * The input string is mangled as follows:
 * %p replaced by the content of the "prefix" option
 * %i replaced by the content of the "tblsslim_data" option
 * %t replaced by the content of the "tblssslim_index" option
 * %m replaced by "UNLOGGED" if the "unlogged" option is set
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
static void set_prefix_and_tbls(options_t const *options, char const **string)
{
    char buffer[1024];
    const char *source;
    char *dest;
    char *openbrace = nullptr;
    int copied = 0;

    if (*string == nullptr) {
        return;
    }
    source = *string;
    dest = buffer;

    while (*source) {
        if (*source == '{') {
            openbrace = dest;
            copied = 0;
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
                    copied = 1;
                }
                source += 2;
                continue;
            } else if (*(source + 1) == 't') {
                if (options->tblsslim_data) {
                    strcpy(dest, options->tblsslim_data->c_str());
                    dest += strlen(options->tblsslim_data->c_str());
                    copied = 1;
                }
                source += 2;
                continue;
            } else if (*(source + 1) == 'i') {
                if (options->tblsslim_index) {
                    strcpy(dest, options->tblsslim_index->c_str());
                    dest += strlen(options->tblsslim_index->c_str());
                    copied = 1;
                }
                source += 2;
                continue;
            } else if (*(source + 1) == 'm') {
                if (options->unlogged) {
                    strcpy(dest, "UNLOGGED");
                    dest += 8;
                    copied = 1;
                }
                source += 2;
                continue;
            }
        }
        *(dest++) = *(source++);
    }
    *dest = 0;
    *string = strdup(buffer);
}

middle_pgsql_t::table_desc::table_desc(const char *name_, const char *create_,
                                       const char *prepare_,
                                       const char *prepare_intarray_,
                                       const char *array_indexes_)
: name(name_), create(create_), prepare(prepare_),
  prepare_intarray(prepare_intarray_), array_indexes(array_indexes_),
  copyMode(0), sql_conn(nullptr), transactionMode(0)
{}

void middle_pgsql_t::table_desc::connect(options_t const *options)
{
    set_prefix_and_tbls(options, &name);
    set_prefix_and_tbls(options, &create);
    set_prefix_and_tbls(options, &prepare);
    set_prefix_and_tbls(options, &prepare_intarray);
    set_prefix_and_tbls(options, &array_indexes);

    fprintf(stderr, "Setting up table: %s\n", name);
    sql_conn = PQconnectdb(options->database_options.conninfo().c_str());

    // Check to see that the backend connection was successfully made, and if not, exit */
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n",
                PQerrorMessage(sql_conn));
        util::exit_nicely();
    }

    /*
     *
     * To allow for parallelisation, the second phase (iterate_ways), cannot be run
     * in an extended transaction and each update statement is its own transaction.
     * Therefore commit rate of postgresql is very important to ensure high speed.
     * If fsync is enabled to ensure safe transactions, the commit rate can be very low.
     * To compensate for this, one can set the postgresql parameter synchronous_commit
     * to off. This means an update statement returns to the client as success before the
     * transaction is saved to disk via fsync, which in return allows to bunch up multiple
     * transactions into a single fsync. This may result in some data loss in the case of a
     * database crash. However, as we don't currently have the ability to restart a full osm2pgsql
     * import session anyway, this is fine. Diff imports are also not effected, as the next
     * diff import would simply deal with all pending ways that were not previously finished.
     * This parameter does not effect safety from data corruption on the back-end.
     */
    pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");
    pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET client_min_messages = WARNING");
}

void middle_pgsql_t::table_desc::begin_copy()
{
    pgsql_exec(sql_conn, PGRES_COPY_IN, "COPY %s FROM STDIN", name);
    copyMode = 1;
}

void middle_pgsql_t::table_desc::end_copy()
{
    // Terminate any pending COPY */
    if (copyMode) {
        int stop = PQputCopyEnd(sql_conn, nullptr);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", name,
                    PQerrorMessage(sql_conn));
            util::exit_nicely();
        }

        pg_result_t res(PQgetResult(sql_conn));
        if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", name,
                    PQerrorMessage(sql_conn));
            util::exit_nicely();
        }
        copyMode = 0;
    }
}

void middle_pgsql_t::table_desc::stop(bool droptemp, bool build_indexes)
{
    time_t start, end;

    fprintf(stderr, "Stopping table: %s\n", name);

    end_copy();

    time(&start);
    if (droptemp) {
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE %s", name);
    } else if (build_indexes && array_indexes) {
        fprintf(stderr, "Building index on table: %s\n", name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", array_indexes);
    }

    PQfinish(sql_conn);
    sql_conn = nullptr;
    time(&end);

    fprintf(stderr, "Stopped table: %s in %is\n", name, (int)(end - start));
}

void middle_pgsql_t::table_desc::begin()
{
    pgsql_exec(sql_conn, PGRES_COMMAND_OK, "BEGIN");
    transactionMode = 1;
}

void middle_pgsql_t::table_desc::commit()
{
    end_copy();

    if (transactionMode) {
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "COMMIT");
        transactionMode = 0;
    }
}

middle_pgsql_t::table_desc::~table_desc()
{
    if (sql_conn) {
        PQfinish(sql_conn);
    }
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


void middle_pgsql_t::buffer_store_string(std::string const &in, bool escape)
{
    for (char const c: in) {
        switch (c) {
            case '"':
                if (escape) copy_buffer += "\\";
                copy_buffer += "\\\"";
                break;
            case '\\':
                if (escape) copy_buffer += "\\\\";
                copy_buffer += "\\\\";
                break;
            case '\n':
                if (escape) copy_buffer += "\\";
                copy_buffer += "\\n";
                break;
            case '\r':
                if (escape) copy_buffer += "\\";
                copy_buffer += "\\r";
                break;
            case '\t':
                if (escape) copy_buffer += "\\";
                copy_buffer += "\\t";
                break;
            default:
                copy_buffer += c;
                break;
        }
    }
}

// escape means we return '\N' for copy mode, otherwise we return just nullptr
void middle_pgsql_t::buffer_store_tags(osmium::OSMObject const &obj, bool attrs,
                                       bool escape)
{
    copy_buffer += "{";

    for (auto const &it : obj.tags()) {
        copy_buffer += "\"";
        buffer_store_string(it.key(), escape);
        copy_buffer += "\",\"";
        buffer_store_string(it.value(), escape);
        copy_buffer += "\",";
    }
    if (attrs) {
        taglist_t extra;
        extra.add_attributes(obj);
        for (auto const &it : extra) {
            copy_buffer += "\"";
            copy_buffer += it.key;
            copy_buffer += "\",\"";
            buffer_store_string(it.value.c_str(), escape);
            copy_buffer += "\",";
        }
    }

    copy_buffer[copy_buffer.size() - 1] = '}';
}

void middle_pgsql_t::buffer_correct_params(char const **param, size_t size)
{
    if (copy_buffer.c_str() != param[0]) {
        auto diff = copy_buffer.c_str() - param[0];
        for (size_t i = 0; i < size; ++i) {
            if (param[i]) {
                param[i] += diff;
            }
        }
    }
}

void middle_pgsql_t::local_nodes_set(osmium::Node const &node)
{
    copy_buffer.reserve(node.tags().byte_size() + 100);

    bool copy = tables[NODE_TABLE].copyMode;
    char delim = copy ? '\t' : '\0';
    const char *paramValues[4] = {
        copy_buffer.c_str(),
    };

    copy_buffer = std::to_string(node.id());
    copy_buffer += delim;

    paramValues[1] = paramValues[0] + copy_buffer.size();
    copy_buffer += std::to_string(node.location().y());
    copy_buffer += delim;

    paramValues[2] = paramValues[0] + copy_buffer.size();
    copy_buffer += std::to_string(node.location().x());

    if (copy) {
        copy_buffer += '\n';
        pgsql_CopyData(__FUNCTION__, tables[NODE_TABLE].sql_conn, copy_buffer);
    } else {
        buffer_correct_params(paramValues, 4);
        pgsql_execPrepared(tables[NODE_TABLE].sql_conn, "insert_node", 3,
                           (const char *const *)paramValues, PGRES_COMMAND_OK);
    }
}

size_t middle_pgsql_t::local_nodes_get_list(osmium::WayNodeList *nodes) const
{
    size_t count = 0;
    std::string buffer("{");

    // get nodes where possible from cache,
    // at the same time build a list for querying missing nodes from DB
    size_t pos = 0;
    for (auto &n : *nodes) {
        auto loc = cache->get(n.ref());
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
    assert(tables[NODE_TABLE].copyMode == 0);

    PGconn *sql_conn = tables[NODE_TABLE].sql_conn;

    char const *paramValues[1];
    paramValues[0] = buffer.c_str();
    auto res = pgsql_execPrepared(sql_conn, "get_node_list", 1, paramValues,
                                  PGRES_TUPLES_OK);
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
        local_nodes_set(node);
    }
}

size_t middle_pgsql_t::nodes_get_list(osmium::WayNodeList *nodes) const
{
    return (out_options->flat_node_cache_enabled)
        ? persistent_cache->get_list(nodes)
        : local_nodes_get_list(nodes);
}

void middle_pgsql_t::local_nodes_delete(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    tables[NODE_TABLE].end_copy();

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(tables[NODE_TABLE].sql_conn, "delete_node", 1,
                       paramValues, PGRES_COMMAND_OK);
}

void middle_pgsql_t::nodes_delete(osmid_t osm_id)
{
    if (out_options->flat_node_cache_enabled) {
        persistent_cache->set(osm_id, osmium::Location());
    } else {
        local_nodes_delete(osm_id);
    }
}

void middle_pgsql_t::node_changed(osmid_t osm_id)
{
    if (!mark_pending) {
        return;
    }

    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    tables[WAY_TABLE].end_copy();
    tables[REL_TABLE].end_copy();

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;

    //keep track of whatever ways and rels these nodes intersect
    //TODO: dont need to stop the copy above since we are only reading?
    auto res =
        pgsql_execPrepared(tables[WAY_TABLE].sql_conn, "mark_ways_by_node", 1,
                           paramValues, PGRES_TUPLES_OK);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }

    //do the rels too
    res = pgsql_execPrepared(tables[REL_TABLE].sql_conn, "mark_rels_by_node", 1,
                             paramValues, PGRES_TUPLES_OK);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
}

void middle_pgsql_t::ways_set(osmium::Way const &way)
{
    copy_buffer.reserve(way.nodes().size() * 10 + way.tags().byte_size() + 100);
    bool copy = tables[WAY_TABLE].copyMode;
    char delim = copy ? '\t' : '\0';
    // Three params: id, nodes, tags */
    const char *paramValues[4] = { copy_buffer.c_str(), };

    copy_buffer = std::to_string(way.id());
    copy_buffer += delim;

    paramValues[1] = paramValues[0] + copy_buffer.size();
    if (way.nodes().size() == 0) {
        copy_buffer += "{}";
    } else {
        copy_buffer += "{";
        for (auto const &n : way.nodes()) {
            copy_buffer += std::to_string(n.ref());
            copy_buffer += ',';
        }
        copy_buffer[copy_buffer.size() - 1] = '}';
    }
    copy_buffer += delim;

    if (way.tags().empty() && !out_options->extra_attributes) {
        paramValues[2] = nullptr;
        copy_buffer += "\\N";
    } else {
        paramValues[2] = paramValues[0] + copy_buffer.size();
        buffer_store_tags(way, out_options->extra_attributes, copy);
    }

    if (copy) {
        copy_buffer += '\n';
        pgsql_CopyData(__FUNCTION__, tables[WAY_TABLE].sql_conn, copy_buffer);
    } else {
        buffer_correct_params(paramValues, 3);
        pgsql_execPrepared(tables[WAY_TABLE].sql_conn, "insert_way", 3,
                           (const char *const *)paramValues, PGRES_COMMAND_OK);
    }
}

bool middle_pgsql_t::ways_get(osmid_t id, osmium::memory::Buffer &buffer) const
{
    char const *paramValues[1];
    PGconn *sql_conn = tables[WAY_TABLE].sql_conn;

    // Make sure we're out of copy mode
    assert(tables[WAY_TABLE].copyMode == 0);

    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    auto res = pgsql_execPrepared(sql_conn, "get_way", 1, paramValues,
                                  PGRES_TUPLES_OK);

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

size_t middle_pgsql_t::rel_way_members_get(osmium::Relation const &rel,
                                           rolelist_t *roles,
                                           osmium::memory::Buffer &buffer) const
{
    char tmp[16];
    char const *paramValues[1];

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
    assert(tables[WAY_TABLE].copyMode == 0);

    PGconn *sql_conn = tables[WAY_TABLE].sql_conn;

    paramValues[0] = tmp2.c_str();
    auto res = pgsql_execPrepared(sql_conn, "get_way_list", 1, paramValues,
                                  PGRES_TUPLES_OK);
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
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    tables[WAY_TABLE].end_copy();

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(tables[WAY_TABLE].sql_conn, "delete_way", 1, paramValues,
                       PGRES_COMMAND_OK);
}

void middle_pgsql_t::iterate_ways(middle_t::pending_processor& pf)
{

    // Make sure we're out of copy mode */
    tables[WAY_TABLE].end_copy();

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
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    tables[REL_TABLE].end_copy();

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;

    //keep track of whatever rels this way intersects
    //TODO: dont need to stop the copy above since we are only reading?
    auto res =
        pgsql_execPrepared(tables[REL_TABLE].sql_conn, "mark_rels_by_way", 1,
                           paramValues, PGRES_TUPLES_OK);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
}

void middle_pgsql_t::relations_set(osmium::Relation const &rel)
{
    idlist_t parts[3];

    for (auto const &m : rel.members()) {
        parts[osmium::item_type_to_nwr_index(m.type())].push_back(m.ref());
    }

    copy_buffer.reserve(rel.members().byte_size() * 2 + rel.tags().byte_size() + 128);

    // Params: id, way_off, rel_off, parts, members, tags */
    const char *paramValues[6] = { copy_buffer.c_str(), };
    bool copy = tables[REL_TABLE].copyMode;
    char delim = copy ? '\t' : '\0';

    copy_buffer = std::to_string(rel.id());
    copy_buffer+= delim;

    paramValues[1] = paramValues[0] + copy_buffer.size();
    copy_buffer += std::to_string(parts[0].size());
    copy_buffer+= delim;

    paramValues[2] = paramValues[0] + copy_buffer.size();
    copy_buffer += std::to_string(parts[0].size() + parts[1].size());
    copy_buffer+= delim;

    paramValues[3] = paramValues[0] + copy_buffer.size();
    if (rel.members().empty()) {
        copy_buffer += "{}";
    } else {
        copy_buffer += "{";
        for (int i = 0; i < 3; ++i) {
            for (auto it : parts[i]) {
                copy_buffer += std::to_string(it);
                copy_buffer += ',';
            }
        }
        copy_buffer[copy_buffer.size() - 1] = '}';
    }
    copy_buffer+= delim;

    if (rel.members().empty()) {
        paramValues[4] = nullptr;
        copy_buffer += "\\N";
    } else {
        paramValues[4] = paramValues[0] + copy_buffer.size();
        copy_buffer += "{";
        for (auto const &m : rel.members()) {
            copy_buffer += '"';
            copy_buffer += osmium::item_type_to_char(m.type());
            copy_buffer += std::to_string(m.ref());
            copy_buffer += "\",\"";
            buffer_store_string(m.role(), copy);
            copy_buffer += "\",";
        }
        copy_buffer[copy_buffer.size() - 1] = '}';
    }
    copy_buffer+= delim;

    if (rel.tags().empty() && !out_options->extra_attributes) {
        paramValues[5] = nullptr;
        copy_buffer += "\\N";
    } else {
        paramValues[5] = paramValues[0] + copy_buffer.size();
        buffer_store_tags(rel, out_options->extra_attributes, copy);
    }

    if (copy) {
        copy_buffer+= '\n';
        pgsql_CopyData(__FUNCTION__, tables[REL_TABLE].sql_conn, copy_buffer);
    } else {
        buffer_correct_params(paramValues, 6);
        pgsql_execPrepared(tables[REL_TABLE].sql_conn, "insert_rel", 6,
                           (const char *const *)paramValues, PGRES_COMMAND_OK);
    }
}

bool middle_pgsql_t::relations_get(osmid_t id, osmium::memory::Buffer &buffer) const
{
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = tables[REL_TABLE].sql_conn;
    taglist_t member_temp;

    // Make sure we're out of copy mode
    assert(tables[REL_TABLE].copyMode == 0);

    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, id);
    paramValues[0] = tmp;

    auto res = pgsql_execPrepared(sql_conn, "get_rel", 1, paramValues,
                                  PGRES_TUPLES_OK);
    // Fields are: members, tags, member_count */

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
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    tables[WAY_TABLE].end_copy();
    tables[REL_TABLE].end_copy();

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;
    pgsql_execPrepared(tables[REL_TABLE].sql_conn, "delete_rel", 1, paramValues,
                       PGRES_COMMAND_OK);

    //keep track of whatever ways this relation interesects
    //TODO: dont need to stop the copy above since we are only reading?
    auto res =
        pgsql_execPrepared(tables[WAY_TABLE].sql_conn, "mark_ways_by_rel", 1,
                           paramValues, PGRES_TUPLES_OK);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }
}

void middle_pgsql_t::iterate_relations(pending_processor& pf)
{
    // Make sure we're out of copy mode */
    tables[REL_TABLE].end_copy();

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
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    tables[REL_TABLE].end_copy();

    sprintf( buffer, "%" PRIdOSMID, osm_id );
    paramValues[0] = buffer;

    //keep track of whatever ways and rels these nodes intersect
    //TODO: dont need to stop the copy above since we are only reading?
    //TODO: can we just mark the id without querying? the where clause seems intersect reltable.parts with the id
    auto res = pgsql_execPrepared(tables[REL_TABLE].sql_conn, "mark_rels", 1,
                                  paramValues, PGRES_TUPLES_OK);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        rels_pending_tracker->mark(marked);
    }
}

idlist_t middle_pgsql_t::relations_using_way(osmid_t way_id) const
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    assert(tables[REL_TABLE].copyMode == 0);

    sprintf(buffer, "%" PRIdOSMID, way_id);
    paramValues[0] = buffer;

    auto result =
        pgsql_execPrepared(tables[REL_TABLE].sql_conn, "rels_using_way", 1,
                           paramValues, PGRES_TUPLES_OK);
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
    for (auto &table : tables) {
        pgsql_exec(table.sql_conn, PGRES_COMMAND_OK, "ANALYZE %s", table.name);
    }
}

void middle_pgsql_t::start(const options_t *out_options_)
{
    out_options = out_options_;
    bool dropcreate = !out_options->append; ///< If tables need to be dropped and created anew

    ways_pending_tracker.reset(new id_tracker());
    rels_pending_tracker.reset(new id_tracker());

    // Gazetter doesn't use mark-pending processing and consequently
    // needs no way-node index.
    // TODO Currently, set here to keep the impact on the code small.
    // We actually should have the output plugins report their needs
    // and pass that via the constructor to middle_t, so that middle_t
    // itself doesn't need to know about details of the output.
    if (out_options->output_backend == "gazetteer") {
        tables[WAY_TABLE].array_indexes = nullptr;
        mark_pending = false;
    }

    append = out_options->append;

    cache.reset(new node_ram_cache(out_options->alloc_chunkwise | ALLOC_LOSSY,
                                   out_options->cache));

    if (out_options->flat_node_cache_enabled) {
        persistent_cache.reset(new node_persistent_cache(out_options, cache));
    }

    fprintf(stderr, "Mid: pgsql, cache=%d\n", out_options->cache);

    // We use a connection per table to enable the use of COPY */
    for (auto &table : tables) {
        table.connect(out_options);
        PGconn* sql_conn = table.sql_conn;

        if (dropcreate) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s CASCADE", table.name);
        }

        table.begin();

        if (dropcreate && table.create) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.create);
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "RESET client_min_messages");

        if (table.prepare) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.prepare);
        }

        if (append && table.prepare_intarray) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.prepare_intarray);
        }

        table.begin_copy();
    }
}

void middle_pgsql_t::commit()
{
    for (auto &table : tables) {
        table.commit();
    }
}

void middle_pgsql_t::flush()
{
    for (auto &table : tables) {
        table.end_copy();
    }
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
            t.stop(out_options->droptemp, !append);
        }
    } else {
        for (auto &t : tables) {
            pool.submit(std::bind(&middle_pgsql_t::table_desc::stop,
                                  &t, out_options->droptemp, !append));
        }
    }
}

middle_pgsql_t::middle_pgsql_t()
: append(false), mark_pending(true)
{
    // clang-format off
    /*table = t_node,*/
    tables[NODE_TABLE] = table_desc(
            /*name*/ "%p_nodes",
          /*create*/ "CREATE %m TABLE %p_nodes (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, lat int4 not null, lon int4 not null) {TABLESPACE %t};\n",
         /*prepare*/ "PREPARE insert_node (" POSTGRES_OSMID_TYPE ", int4, int4) AS INSERT INTO %p_nodes VALUES ($1,$2,$3);\n"
               "PREPARE get_node_list(" POSTGRES_OSMID_TYPE "[]) AS SELECT id, lat, lon FROM %p_nodes WHERE id = ANY($1::" POSTGRES_OSMID_TYPE "[]);\n"
               "PREPARE delete_node (" POSTGRES_OSMID_TYPE ") AS DELETE FROM %p_nodes WHERE id = $1;\n",
/*prepare_intarray*/ nullptr
                         );
    tables[WAY_TABLE] = table_desc(
        /*table t_way,*/
            /*name*/ "%p_ways",
          /*create*/ "CREATE %m TABLE %p_ways (id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, nodes " POSTGRES_OSMID_TYPE "[] not null, tags text[]) {TABLESPACE %t};\n",
         /*prepare*/ "PREPARE insert_way (" POSTGRES_OSMID_TYPE ", " POSTGRES_OSMID_TYPE "[], text[]) AS INSERT INTO %p_ways VALUES ($1,$2,$3);\n"
               "PREPARE get_way (" POSTGRES_OSMID_TYPE ") AS SELECT nodes, tags, array_upper(nodes,1) FROM %p_ways WHERE id = $1;\n"
               "PREPARE get_way_list (" POSTGRES_OSMID_TYPE "[]) AS SELECT id, nodes, tags, array_upper(nodes,1) FROM %p_ways WHERE id = ANY($1::" POSTGRES_OSMID_TYPE "[]);\n"
               "PREPARE delete_way(" POSTGRES_OSMID_TYPE ") AS DELETE FROM %p_ways WHERE id = $1;\n",
/*prepare_intarray*/
               "PREPARE mark_ways_by_node(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE nodes && ARRAY[$1];\n"
               "PREPARE mark_ways_by_rel(" POSTGRES_OSMID_TYPE ") AS select id from %p_ways WHERE id IN (SELECT unnest(parts[way_off+1:rel_off]) FROM %p_rels WHERE id = $1);\n",

   /*array_indexes*/ "CREATE INDEX %p_ways_nodes ON %p_ways USING gin (nodes) WITH (FASTUPDATE=OFF) {TABLESPACE %i};\n"
                         );
    tables[REL_TABLE] = table_desc(
        /*table = t_rel,*/
            /*name*/ "%p_rels",
          /*create*/ "CREATE %m TABLE %p_rels(id " POSTGRES_OSMID_TYPE " PRIMARY KEY {USING INDEX TABLESPACE %i}, way_off int2, rel_off int2, parts " POSTGRES_OSMID_TYPE "[], members text[], tags text[]) {TABLESPACE %t};\n",
         /*prepare*/ "PREPARE insert_rel (" POSTGRES_OSMID_TYPE ", int2, int2, " POSTGRES_OSMID_TYPE "[], text[], text[]) AS INSERT INTO %p_rels VALUES ($1,$2,$3,$4,$5,$6);\n"
               "PREPARE get_rel (" POSTGRES_OSMID_TYPE ") AS SELECT members, tags, array_upper(members,1)/2 FROM %p_rels WHERE id = $1;\n"
               "PREPARE delete_rel(" POSTGRES_OSMID_TYPE ") AS DELETE FROM %p_rels WHERE id = $1;\n",
/*prepare_intarray*/
                "PREPARE rels_using_way(" POSTGRES_OSMID_TYPE ") AS SELECT id FROM %p_rels WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1];\n"
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

    // Return a copy of the original, as we need separate database connections.
    std::unique_ptr<middle_pgsql_t> mid(new middle_pgsql_t());
    mid->out_options = src->out_options;
    mid->append = src->out_options->append;
    mid->mark_pending = src->mark_pending;

    // NOTE: this is thread safe for use in pending async processing only because
    // during that process they are only read from
    mid->cache = src->cache;
    mid->persistent_cache = src->persistent_cache;

    // We use a connection per table to enable the use of COPY
    for (int i = 0; i < NUM_TABLES; i++) {
        mid->tables[i].connect(mid->out_options);
        PGconn* sql_conn = mid->tables[i].sql_conn;

        if (mid->tables[i].prepare) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s",
                       mid->tables[i].prepare);
        }

        if (mid->append && mid->tables[i].prepare_intarray) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s",
                       mid->tables[i].prepare_intarray);
        }
    }

    return std::shared_ptr<middle_query_t>(mid.release());
}

size_t middle_pgsql_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}
