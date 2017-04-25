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
#include <future>

#include <boost/format.hpp>
#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/memory/buffer.hpp>

#include <libpq-fe.h>

#include "middle-pgsql.hpp"
#include "node-persistent-cache.hpp"
#include "node-ram-cache.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-pgsql.hpp"
#include "pgsql.hpp"
#include "util.hpp"

typedef boost::format fmt;

enum table_id
{
    t_node,
    t_way,
    t_rel
};


std::string escape_string(std::string const &in, bool escape)
{
    std::string result;
    for (char const c : in) {
        switch (c) {
        case '"':
            if (escape)
                result += "\\";
            result += "\\\"";
            break;
        case '\\':
            if (escape)
                result += "\\\\";
            result += "\\\\";
            break;
        case '\n':
            if (escape)
                result += "\\";
            result += "\\n";
            break;
        case '\r':
            if (escape)
                result += "\\";
            result += "\\r";
            break;
        case '\t':
            if (escape)
                result += "\\";
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }
    return result;
}


class tags_storage_t {
public:
    virtual std::string get_column_name()=0;

virtual void pgsql_parse_tags(const char *string, osmium::builder::TagListBuilder & builder) = 0;

virtual std::string encode_tags(osmium::OSMObject const &obj, bool attrs,
                                       bool escape) = 0;

virtual ~tags_storage_t(){};

};

class jsonb_tags_storage_t : public tags_storage_t{


// Decodes a portion of an array literal from postgres */
// Argument should point to beginning of literal, on return points to delimiter */
inline const char *decode_upto(const char *src, char *dst)
{
    while (*src == ' ')
        src++;
    int quoted = (*src == '"');
    if (quoted)
        src++;

    while (quoted ? (*src != '"')
                  : (*src != ',' && *src != '}' && *src != ':')) {
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
        } else
            *dst++ = *src++;
    }
    if (quoted)
        src++;
    *dst = 0;
    return src;
}

public:
    std::string get_column_name() {return "jsonb";}

void pgsql_parse_tags(const char *string, osmium::builder::TagListBuilder & builder){
    if (*string++ != '{')
        return;

    char key[1024];
    char val[1024];

    while (*string != '}') {
        string = decode_upto(string, key);
        // String points to the comma */
        string++;
        string = decode_upto(string, val);
        builder.add_tag(key, val);
        // String points to the comma or closing '}' */
        if (*string == ',') {
            string++;
        }
    }
}

// escape means we return '\N' for copy mode, otherwise we return just nullptr
std::string encode_tags(osmium::OSMObject const &obj, bool attrs,
                                       bool escape)
{
    std::string result = "{";
    for (auto const &it : obj.tags()) {
        result += (fmt("\"%1%\": \"%2%\",") % escape_string(it.key(), escape) % escape_string(it.value(), escape)).str();
    }
    if (attrs) {
        taglist_t extra;
        extra.add_attributes(obj);
        for (auto const &it : extra) {
            result += (fmt("\"%1%\": \"%2%\",") % it.key % escape_string(it.value, escape)).str();
        }
    }

    result[result.size() - 1] = '}';
    return result;
}

~jsonb_tags_storage_t(){}
};

class hstore_tags_storage_t : public tags_storage_t {

// Decodes a portion of an array literal from postgres */
// Argument should point to beginning of literal, on return points to delimiter */
inline const char *decode_upto(const char *src, char *dst)
{
    while (*src == ' ')
        src++;
    int quoted = (*src == '"');
    if (quoted)
        src++;

    while (quoted ? (*src != '"')
                  : (*src != ',' && *src != '}' && *src != ':')) {
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
        } else
            *dst++ = *src++;
    }
    if (quoted)
        src++;
    *dst = 0;
    return src;
}

// TODO! copypasted from table.cpp. Extract to orginal lib.
//create an escaped version of the string for hstore table insert
void escape4hstore(const char *src, std::string& dst)
{
    dst.push_back('"');
    for (size_t i = 0; i < strlen(src); ++i) {
        switch (src[i]) {
            case '\\':
                dst.append("\\\\\\\\");
                break;
            case '"':
                dst.append("\\\\\"");
                break;
            case '\t':
                dst.append("\\\t");
                break;
            case '\r':
                dst.append("\\\r");
                break;
            case '\n':
                dst.append("\\\n");
                break;
            default:
                dst.push_back(src[i]);
                break;
        }
    }
    dst.push_back('"');
}


public:
    std::string get_column_name() {return "hstore";}

void pgsql_parse_tags(const char *string, osmium::builder::TagListBuilder & builder){
    if (*string != '"')
        return;

    char key[1024];
    char val[1024];

    while (strlen(string)) {
        string = decode_upto(string, key);
        // Find start of the next string
        while (*++string!='"') {}
        string = decode_upto(string, val);
        builder.add_tag(key, val);
        // String points to the comma or end */
        if (*string == ',') {
            string++;
        }
    }
}

// escape means we return '\N' for copy mode, otherwise we return just nullptr
std::string encode_tags(osmium::OSMObject const &obj, bool attrs,
                                       bool escape)
{
    std::string result;// = "'";
    for (auto const &it : obj.tags()) {
        //result += (fmt("%1%=>%2%,") % escape_string(it.key(), escape) % escape_string(it.value(), escape)).str();
        escape4hstore(it.key(), result);
        result += "=>";
        escape4hstore(it.value(), result);
        result += ',';
    }
    if (attrs) {
        taglist_t extra;
        extra.add_attributes(obj);
        for (auto const &it : extra) {
            //result += (fmt("%1%=>%2%,") % it.key % escape_string(it.value, escape)).str();
            escape4hstore(it.key.c_str(), result);
            result += "=>";
            escape4hstore(it.value.c_str(), result);
            result += ',';
        }
    }

    result[result.size() - 1] = ' ';
    return result;
}

~hstore_tags_storage_t(){}
};

middle_pgsql_t::table_desc::table_desc()
: copyMode(0), transactionMode(0), sql_conn(nullptr)
{
}

namespace {

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

void pgsql_parse_members(const char *string, osmium::memory::Buffer &buffer,
                         osmium::builder::RelationBuilder &obuilder)
{
    if (*string++ != '{')
        return;

    char role[1024];
    osmium::builder::RelationMemberListBuilder builder(buffer, &obuilder);

    while (*string != '}') {
        char type = string[0];
        char *endp;
        osmid_t id = strtoosmid(string + 1, &endp, 10);
        // String points to the comma */
        string = decode_upto(endp + 1, role);
        builder.add_member(osmium::char_to_item_type(type), id, role);
        // String points to the comma or closing '}' */
        if (*string == ',') {
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

int pgsql_endCopy(middle_pgsql_t::table_desc *table)
{
    // Terminate any pending COPY */
    if (table->copyMode) {
        PGconn *sql_conn = table->sql_conn;
        int stop = PQputCopyEnd(sql_conn, nullptr);
        if (stop != 1) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", table->copy.c_str(),
                    PQerrorMessage(sql_conn));
            util::exit_nicely();
        }

        pg_result_t res(PQgetResult(sql_conn));
        if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
            fprintf(stderr, "COPY_END for %s failed: %s\n", table->copy.c_str(),
                    PQerrorMessage(sql_conn));
            util::exit_nicely();
        }
        table->copyMode = 0;
    }
    return 0;
}
} // anonymous namespace

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

    bool copy = node_table->copyMode;
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
        pgsql_CopyData(__FUNCTION__, node_table->sql_conn, copy_buffer);
    } else {
        buffer_correct_params(paramValues, 4);
        pgsql_execPrepared(node_table->sql_conn, "insert_node", 3,
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

    pgsql_endCopy(node_table);

    PGconn *sql_conn = node_table->sql_conn;

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
    pgsql_endCopy(node_table);

    sprintf(buffer, "%" PRIdOSMID, osm_id);
    paramValues[0] = buffer;
    pgsql_execPrepared(node_table->sql_conn, "delete_node", 1, paramValues,
                       PGRES_COMMAND_OK);
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
    pgsql_endCopy(way_table);
    pgsql_endCopy(rel_table);

    sprintf(buffer, "%" PRIdOSMID, osm_id);
    paramValues[0] = buffer;

    //keep track of whatever ways and rels these nodes intersect
    //TODO: dont need to stop the copy above since we are only reading?
    auto res = pgsql_execPrepared(way_table->sql_conn, "mark_ways_by_node", 1,
                                  paramValues, PGRES_TUPLES_OK);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }

    //do the rels too
    res = pgsql_execPrepared(rel_table->sql_conn, "mark_rels_by_node", 1,
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
    bool copy = way_table->copyMode;
    char delim = copy ? '\t' : '\0';
    // Three params: id, nodes, tags */
    const char *paramValues[4] = {
        copy_buffer.c_str(),
    };

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
        copy_buffer += tags_storage->encode_tags(way, out_options->extra_attributes, copy);
    }

    if (copy) {
        copy_buffer += '\n';
        pgsql_CopyData(__FUNCTION__, way_table->sql_conn, copy_buffer);
    } else {
        buffer_correct_params(paramValues, 3);
        pgsql_execPrepared(way_table->sql_conn, "insert_way", 3,
                           (const char *const *)paramValues, PGRES_COMMAND_OK);
    }
}

bool middle_pgsql_t::ways_get(osmid_t id, osmium::memory::Buffer &buffer) const
{
    char const *paramValues[1];
    PGconn *sql_conn = way_table->sql_conn;

    // Make sure we're out of copy mode */
    pgsql_endCopy(way_table);

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

        osmium::builder::TagListBuilder tl_builder(buffer, &builder);
        tags_storage->pgsql_parse_tags(PQgetvalue(res.get(), 0, 1), tl_builder);
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

    pgsql_endCopy(way_table);

    PGconn *sql_conn = way_table->sql_conn;

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
                    osmium::builder::TagListBuilder tl_builder(buffer, &builder);
                    tags_storage->pgsql_parse_tags(PQgetvalue(res.get(), j, 2), tl_builder);
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
    pgsql_endCopy(way_table);

    sprintf(buffer, "%" PRIdOSMID, osm_id);
    paramValues[0] = buffer;
    pgsql_execPrepared(way_table->sql_conn, "delete_way", 1, paramValues,
                       PGRES_COMMAND_OK);
}

void middle_pgsql_t::iterate_ways(middle_t::pending_processor &pf)
{

    // Make sure we're out of copy mode */
    pgsql_endCopy(way_table);

    // enqueue the jobs
    osmid_t id;
    while (id_tracker::is_valid(id = ways_pending_tracker->pop_mark())) {
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
    pgsql_endCopy(rel_table);

    sprintf(buffer, "%" PRIdOSMID, osm_id);
    paramValues[0] = buffer;

    //keep track of whatever rels this way intersects
    //TODO: dont need to stop the copy above since we are only reading?
    auto res = pgsql_execPrepared(rel_table->sql_conn, "mark_rels_by_way", 1,
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

    copy_buffer.reserve(rel.members().byte_size() * 2 + rel.tags().byte_size() +
                        128);

    // Params: id, way_off, rel_off, parts, members, tags */
    const char *paramValues[6] = {
        copy_buffer.c_str(),
    };
    bool copy = rel_table->copyMode;
    char delim = copy ? '\t' : '\0';

    copy_buffer = std::to_string(rel.id());
    copy_buffer += delim;

    paramValues[1] = paramValues[0] + copy_buffer.size();
    copy_buffer += std::to_string(parts[0].size());
    copy_buffer += delim;

    paramValues[2] = paramValues[0] + copy_buffer.size();
    copy_buffer += std::to_string(parts[0].size() + parts[1].size());
    copy_buffer += delim;

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
    copy_buffer += delim;

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
            copy_buffer += escape_string(m.role(), copy);
            copy_buffer += "\",";
        }
        copy_buffer[copy_buffer.size() - 1] = '}';
    }
    copy_buffer += delim;

    if (rel.tags().empty() && !out_options->extra_attributes) {
        paramValues[5] = nullptr;
        copy_buffer += "\\N";
    } else {
        paramValues[5] = paramValues[0] + copy_buffer.size();
        copy_buffer += tags_storage->encode_tags(rel, out_options->extra_attributes, copy);
    }

    if (copy) {
        copy_buffer += '\n';
        pgsql_CopyData(__FUNCTION__, rel_table->sql_conn, copy_buffer);
    } else {
        buffer_correct_params(paramValues, 6);
        pgsql_execPrepared(rel_table->sql_conn, "insert_rel", 6,
                           (const char *const *)paramValues, PGRES_COMMAND_OK);
    }
}

bool middle_pgsql_t::relations_get(osmid_t id,
                                   osmium::memory::Buffer &buffer) const
{
    char tmp[16];
    char const *paramValues[1];
    PGconn *sql_conn = rel_table->sql_conn;
    taglist_t member_temp;

    // Make sure we're out of copy mode */
    pgsql_endCopy(rel_table);

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
        osmium::builder::TagListBuilder tl_builder(buffer, &builder);
        tags_storage->pgsql_parse_tags(PQgetvalue(res.get(), 0, 1), tl_builder);
    }

    buffer.commit();

    return true;
}

void middle_pgsql_t::relations_delete(osmid_t osm_id)
{
    char const *paramValues[1];
    char buffer[64];
    // Make sure we're out of copy mode */
    pgsql_endCopy(way_table);
    pgsql_endCopy(rel_table);

    sprintf(buffer, "%" PRIdOSMID, osm_id);
    paramValues[0] = buffer;
    pgsql_execPrepared(rel_table->sql_conn, "delete_rel", 1, paramValues,
                       PGRES_COMMAND_OK);

    //keep track of whatever ways this relation interesects
    //TODO: dont need to stop the copy above since we are only reading?
    auto res = pgsql_execPrepared(way_table->sql_conn, "mark_ways_by_rel", 1,
                                  paramValues, PGRES_TUPLES_OK);
    for (int i = 0; i < PQntuples(res.get()); ++i) {
        char *end;
        osmid_t marked = strtoosmid(PQgetvalue(res.get(), i, 0), &end, 10);
        ways_pending_tracker->mark(marked);
    }
}

void middle_pgsql_t::iterate_relations(pending_processor &pf)
{
    // Make sure we're out of copy mode */
    pgsql_endCopy(rel_table);

    // enqueue the jobs
    osmid_t id;
    while (id_tracker::is_valid(id = rels_pending_tracker->pop_mark())) {
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
    pgsql_endCopy(rel_table);

    sprintf(buffer, "%" PRIdOSMID, osm_id);
    paramValues[0] = buffer;

    //keep track of whatever ways and rels these nodes intersect
    //TODO: dont need to stop the copy above since we are only reading?
    //TODO: can we just mark the id without querying? the where clause seems intersect reltable.parts with the id
    auto res = pgsql_execPrepared(rel_table->sql_conn, "mark_rels", 1,
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
    pgsql_endCopy(rel_table);

    sprintf(buffer, "%" PRIdOSMID, way_id);
    paramValues[0] = buffer;

    auto result = pgsql_execPrepared(rel_table->sql_conn, "rels_using_way", 1,
                                     paramValues, PGRES_TUPLES_OK);
    const int ntuples = PQntuples(result.get());
    idlist_t rel_ids;
    rel_ids.resize((size_t)ntuples);
    for (int i = 0; i < ntuples; ++i) {
        rel_ids[i] = strtoosmid(PQgetvalue(result.get(), i, 0), nullptr, 10);
    }

    return rel_ids;
}

void middle_pgsql_t::analyze(void)
{
    for (auto &table : tables) {
        PGconn *sql_conn = table.sql_conn;

        if (!table.analyze.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.analyze.c_str());
        }
    }
}

void middle_pgsql_t::end(void)
{
    for (auto &table : tables) {
        PGconn *sql_conn = table.sql_conn;

        // Commit transaction */
        if (!table.stop.empty() && table.transactionMode) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.stop.c_str());
            table.transactionMode = 0;
        }
    }
}

//TODO extract to file?
std::string get_tablespace_str(const options_t &options)
{
    if (options.tblsslim_data) {
        return std::string("TABLESPACE ") + std::string(*options.tblsslim_data);
    } else {
        return std::string();
    }
}

std::string get_tablespace_index_str(const options_t &options)
{
    if (options.tblsslim_index) {
        return std::string("USING INDEX TABLESPACE ") +
               std::string(*options.tblsslim_index);
    } else {
        return std::string();
    }
}

std::string get_logging_str(const options_t &options)
{
    if (options.unlogged) {
        return std::string("UNLOGGED");
    } else {
        return std::string();
    }
}

void middle_pgsql_t::generate_rels_table_queries(const options_t &options,
                                 middle_pgsql_t::table_desc &table)
{
    table.name = (fmt("%1%_rels") % options.prefix).str();
    table.start = "BEGIN;";
    table.create =
        (fmt("CREATE %1% TABLE %2%_rels(id %3% PRIMARY KEY %4%, way_off int2, "
             "rel_off int2, parts %3% [], members text[], tags %5%) %6%;") %
         get_logging_str(options) % options.prefix % POSTGRES_OSMID_TYPE %
         get_tablespace_index_str(options) % tags_storage->get_column_name() % get_tablespace_str(options))
            .str();
    table.prepare =
        (fmt("PREPARE insert_rel (%1%, int2, int2, %1%[], text[], %3%) AS INSERT INTO %2%_rels VALUES ($1,$2,$3,$4,$5,$6);\n\
               PREPARE get_rel (%1%) AS SELECT members, tags, array_upper(members,1)/2 FROM %2%_rels WHERE id = $1;\n\
               PREPARE delete_rel(%1%) AS DELETE FROM %2%_rels WHERE id = $1;\n") %
         POSTGRES_OSMID_TYPE % options.prefix % tags_storage->get_column_name())
            .str();
    table.prepare_intarray =
        (fmt("PREPARE rels_using_way(%1%) AS SELECT id FROM %2%_rels WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1];\n\
                PREPARE mark_rels_by_node(%1%) AS select id from %2%_ways WHERE nodes && ARRAY[$1];\n\
                PREPARE mark_rels_by_way(%1%) AS select id from %2%_rels WHERE parts && ARRAY[$1] AND parts[way_off+1:rel_off] && ARRAY[$1];\n\
                PREPARE mark_rels(%1%) AS select id from %2%_rels WHERE parts && ARRAY[$1] AND parts[rel_off+1:array_length(parts,1)] && ARRAY[$1];\n") %
         POSTGRES_OSMID_TYPE % options.prefix)
            .str();

    table.copy = (fmt("COPY %1%_rels FROM STDIN;") % options.prefix).str();
    table.analyze = (fmt("ANALYZE %1%_rels;") % options.prefix).str();
    table.stop = "COMMIT;",
    table.array_indexes = (fmt("CREATE INDEX %1%_rels_parts ON %1%_rels USING "
                               "gin (parts) WITH (FASTUPDATE=OFF) %2%;") %
                           options.prefix % get_tablespace_str(options))
                              .str();
}

void middle_pgsql_t::generate_ways_table_queries(const options_t &options,
                                 middle_pgsql_t::table_desc &table)
{
    table.name = (fmt("%1%_ways") % options.prefix).str();
    table.start = "BEGIN;";
    table.create =
        (fmt("CREATE %1% TABLE %2%_ways (id %3% PRIMARY KEY %4%, nodes %3% [] "
             "not null, tags %5%) %6%;") %
         get_logging_str(options) % options.prefix % POSTGRES_OSMID_TYPE %
         get_tablespace_index_str(options) % tags_storage->get_column_name() % get_tablespace_str(options))
            .str();
    table.prepare =
        (fmt("PREPARE Insert_way (%1%, %1%[], %3%) AS INSERT INTO %2%_ways VALUES ($1,$2,$3);\
               PREPARE get_way (%1%) AS SELECT nodes, tags, array_upper(nodes,1) FROM %2%_ways WHERE id = $1;\
               PREPARE get_way_list (%1%[]) AS SELECT id, nodes, tags, array_upper(nodes,1) FROM %2%_ways WHERE id = any($1::%1%[]);\
               PREPARE delete_way(%1%) AS DELETE FROM %2%_ways WHERE id = $1;") %
         POSTGRES_OSMID_TYPE % options.prefix % tags_storage->get_column_name())
            .str();
    table.prepare_intarray =
        (fmt("PREPARE mark_ways_by_node(%1%) AS SELECT id FROM %2%_ways WHERE nodes && array[$1];\
               PREPARE mark_ways_by_rel(%1%) AS SELECT id FROM %2%_ways WHERE id IN (select unnest(parts[way_off+1:rel_off]) FROM %2%_rels WHERE id = $1);") %
         POSTGRES_OSMID_TYPE % options.prefix)
            .str();

    table.copy = (fmt("COPY %1%_ways FROM stdin;") % options.prefix).str();
    table.analyze = (fmt("ANALYZE %1%_ways;") % options.prefix).str();
    table.stop = "COMMIT;";
    table.array_indexes = (fmt("CREATE INDEX %1%_ways_nodes ON %1%_ways USING "
                               "gin (nodes) WITH (fastupdate=off) %2%;") %
                           options.prefix % get_tablespace_str(options))
                              .str();
}

void middle_pgsql_t::generate_nodes_table_queries(const options_t &options,
                                  middle_pgsql_t::table_desc &table)
{
    table.name = (fmt("%1%_nodes") % options.prefix).str();
    table.start = "BEGIN;";
    table.create = (fmt("CREATE %5% TABLE %1%_nodes (id %2% PRIMARY KEY %3%, "
                        "lat int4 not null, lon int4 not null) %4%;") %
                    options.prefix % POSTGRES_OSMID_TYPE %
                    get_tablespace_index_str(options) %
                    get_tablespace_str(options) % get_logging_str(options))
                       .str();
    table.prepare =
        (fmt("PREPARE insert_node (%1%, int4, int4) AS INSERT INTO %2%_nodes VALUES ($1,$2,$3); \
               PREPARE get_node_list(%1% []) AS SELECT id, lat, lon FROM %2%_nodes WHERE id = ANY($1::%1%[]);\
               PREPARE delete_node (%1%) AS DELETE FROM %2%_nodes WHERE id = $1;") %
         POSTGRES_OSMID_TYPE % options.prefix)
            .str();
    table.copy = (fmt("COPY %1%_nodes FROM STDIN;") % options.prefix).str();
    table.analyze = (fmt("ANALYZE %1%_nodes;") % options.prefix).str();
    table.stop = "COMMIT;";
}

void middle_pgsql_t::connect(table_desc &table)
{
    PGconn *sql_conn;

    if (out_options->jsonb_mode) {
        delete tags_storage;
        tags_storage = new jsonb_tags_storage_t();
    }
    generate_nodes_table_queries(*out_options, *node_table);
    generate_ways_table_queries(*out_options, *way_table);
    generate_rels_table_queries(*out_options, *rel_table);

    fprintf(stderr, "Setting up table: %s\n", table.name.c_str());
    sql_conn = PQconnectdb(out_options->database_options.conninfo().c_str());

    // Check to see that the backend connection was successfully made, and if not, exit */
    if (PQstatus(sql_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n",
                PQerrorMessage(sql_conn));
        util::exit_nicely();
    }
    table.sql_conn = sql_conn;
}

void middle_pgsql_t::start(const options_t *out_options_)
{
    out_options = out_options_;
    bool dropcreate =
        !out_options->append; ///< If tables need to be dropped and created anew

    ways_pending_tracker.reset(new id_tracker());
    rels_pending_tracker.reset(new id_tracker());

    // Gazetter doesn't use mark-pending processing and consequently
    // needs no way-node index.
    // TODO Currently, set here to keep the impact on the code small.
    // We actually should have the output plugins report their needs
    // and pass that via the constructor to middle_t, so that middle_t
    // itself doesn't need to know about details of the output.
    if (out_options->output_backend == "gazetteer") {
        way_table->array_indexes = std::string();
        mark_pending = false;
    }

    append = out_options->append;
    // reset this on every start to avoid options from last run
    // staying set for the second.
    build_indexes = !append && !out_options->droptemp;

    cache.reset(new node_ram_cache(out_options->alloc_chunkwise | ALLOC_LOSSY,
                                   out_options->cache));

    if (out_options->flat_node_cache_enabled) {
        persistent_cache.reset(new node_persistent_cache(out_options, cache));
    }

    fprintf(stderr, "Mid: pgsql, cache=%d\n", out_options->cache);

    // We use a connection per table to enable the use of COPY */
    for (auto &table : tables) {
        connect(table);
        PGconn *sql_conn = table.sql_conn;

        /*
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
        pgsql_exec(sql_conn, PGRES_COMMAND_OK,
                   "SET synchronous_commit TO off;");

        pgsql_exec(sql_conn, PGRES_COMMAND_OK,
                   "SET client_min_messages = WARNING");
        if (dropcreate) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK,
                       "DROP TABLE IF EXISTS %s CASCADE", table.name.c_str());
        }

        if (!table.start.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.start.c_str());
            table.transactionMode = 1;
        }

        if (dropcreate && !table.create.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.create.c_str());
            if (!table.create_index.empty()) {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s",
                           table.create_index.c_str());
            }
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "RESET client_min_messages");

        if (!table.prepare.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.prepare.c_str());
        }

        if (append && !table.prepare_intarray.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s",
                       table.prepare_intarray.c_str());
        }

        if (!table.copy.empty()) {
            pgsql_exec(sql_conn, PGRES_COPY_IN, "%s", table.copy.c_str());
            table.copyMode = 1;
        }
    }
}

void middle_pgsql_t::commit(void)
{
    for (auto &table : tables) {
        PGconn *sql_conn = table.sql_conn;
        pgsql_endCopy(&table);
        if (!table.stop.empty() && table.transactionMode) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", table.stop.c_str());
            table.transactionMode = 0;
        }
    }
}

void middle_pgsql_t::pgsql_stop_one(table_desc *table)
{
    time_t start, end;

    PGconn *sql_conn = table->sql_conn;

    fprintf(stderr, "Stopping table: %s\n", table->name.c_str());
    pgsql_endCopy(table);
    time(&start);
    if (out_options->droptemp) {
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE %s",
                   table->name.c_str());
    } else if (build_indexes && !table->array_indexes.empty()) {
        fprintf(stderr, "Building index on table: %s\n", table->name.c_str());
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s",
                   table->array_indexes.c_str());
    }

    PQfinish(sql_conn);
    table->sql_conn = nullptr;
    time(&end);
    fprintf(stderr, "Stopped table: %s in %is\n", table->name.c_str(),
            (int)(end - start));
}

void middle_pgsql_t::stop(void)
{
    cache.reset();
    if (out_options->flat_node_cache_enabled)
        persistent_cache.reset();

    std::vector<std::future<void>> futures;
    futures.reserve(num_tables);

    for (int i = 0; i < num_tables; ++i) {
        futures.push_back(
            std::async(&middle_pgsql_t::pgsql_stop_one, this, &tables[i]));
    }

    for (auto &f : futures) {
        f.get();
    }
}

middle_pgsql_t::middle_pgsql_t()
: num_tables(0), node_table(nullptr), way_table(nullptr), rel_table(nullptr),
  append(false), mark_pending(true), build_indexes(true), tags_storage(new hstore_tags_storage_t())
{
    tables.resize(3);
    // set up the rest of the variables from the tables.
    num_tables = tables.size();
    assert(num_tables == 3);

    node_table = &tables[0];
    way_table = &tables[1];
    rel_table = &tables[2];
}

middle_pgsql_t::~middle_pgsql_t()
{
    for (auto &table : tables) {
        if (table.sql_conn) {
            PQfinish(table.sql_conn);
        }
    }
    delete tags_storage;
}

std::shared_ptr<const middle_query_t> middle_pgsql_t::get_instance() const
{
    middle_pgsql_t *mid = new middle_pgsql_t();
    mid->out_options = out_options;
    mid->append = out_options->append;
    mid->mark_pending = mark_pending;

    //NOTE: this is thread safe for use in pending async processing only because
    //during that process they are only read from
    mid->cache = cache;
    mid->persistent_cache = persistent_cache;

    // We use a connection per table to enable the use of COPY */
    for (int i = 0; i < num_tables; i++) {
        mid->connect(mid->tables[i]);
        PGconn *sql_conn = mid->tables[i].sql_conn;

        if (!tables[i].prepare.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s",
                       tables[i].prepare.c_str());
        }

        if (append && !tables[i].prepare_intarray.empty()) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s",
                       tables[i].prepare_intarray.c_str());
        }
    }

    return std::shared_ptr<const middle_query_t>(mid);
}

size_t middle_pgsql_t::pending_count() const
{
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}
