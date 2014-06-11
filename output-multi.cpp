#include "output-multi.hpp"
#include "taginfo_impl.hpp"

#include <boost/format.hpp>
#include <vector>

namespace {

std::string mk_column_name(const std::string &name, const options_t &options) {
    return (boost::format("%1%_%2%") % options.prefix % name).str();
}

} // anonymous namespace

output_multi_t::output_multi_t(const std::string &name,
                               boost::shared_ptr<geometry_processor> processor_,
                               struct export_list *export_list_,
                               const middle_query_t* mid_, const options_t &options_) 
    : output_t(mid_, options_),
      m_tagtransform(new tagtransform(&m_options)),
      m_export_list(new export_list(*export_list_)),
      m_processor(processor_),
      m_osm_type(m_processor->interests(geometry_processor::interest_node)
                  ? OSMTYPE_NODE : OSMTYPE_WAY),
      m_table(new table_t(m_options.conninfo, mk_column_name(name, m_options), m_processor->column_type(),
                          m_export_list->normal_columns(m_osm_type),
                          m_options.hstore_columns, m_processor->srid(), m_options.scale,
                          m_options.append, m_options.slim, m_options.droptemp,
                          m_options.hstore_mode, m_options.enable_hstore_index,
                          m_options.tblsmain_data, m_options.tblsmain_index)) {
}

output_multi_t::~output_multi_t() {
}

int output_multi_t::start() {
    ways_pending_tracker.reset(new id_tracker());
    ways_done_tracker.reset(new id_tracker());
    rels_pending_tracker.reset(new id_tracker());

    m_table->start();
    return 0;
}

middle_t::way_cb_func *output_multi_t::way_callback() {
    // TODO!
    return NULL;
}

middle_t::rel_cb_func *output_multi_t::relation_callback() {
    // TODO!
    return NULL;
}

void output_multi_t::stop() {
    m_table->stop();
}

void output_multi_t::commit() {
    m_table->commit();

    ways_pending_tracker->commit();
    ways_done_tracker->commit();
    rels_pending_tracker->commit();
}

int output_multi_t::node_add(osmid_t id, double lat, double lon, struct keyval *tags) {
    if (m_processor->interests(geometry_processor::interest_node)) {
        return process_node(id, lat, lon, tags);
    }
    return 0;
}

int output_multi_t::way_add(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    if (m_processor->interests(geometry_processor::interest_way) && node_count > 1) {
        return process_way(id, nodes, node_count, tags);
    }
    return 0;
}


int output_multi_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    if (m_processor->interests(geometry_processor::interest_relation) && member_count > 0) {
        return process_relation(id, members, member_count, tags);
    }
    return 0;
}

int output_multi_t::node_modify(osmid_t id, double lat, double lon, struct keyval *tags) {
    if (m_processor->interests(geometry_processor::interest_node)) {
        // TODO - need to know it's a node?
        delete_from_output(id);

        // TODO: need to mark any ways or relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_node(id, lat, lon, tags);

    } else {
        return 0;
    }
}

int output_multi_t::way_modify(osmid_t id, osmid_t *nodes, int node_count, struct keyval *tags) {
    if (m_processor->interests(geometry_processor::interest_way)) {
        // TODO - need to know it's a way?
        delete_from_output(id);

        // TODO: need to mark any relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_way(id, nodes, node_count, tags);

    } else {
        return 0;
    }
}

int output_multi_t::relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags) {
    if (m_processor->interests(geometry_processor::interest_relation)) {
        // TODO - need to know it's a relation?
        delete_from_output(id);

        // TODO: need to mark any other relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_relation(id, members, member_count, tags);

    } else {
        return 0;
    }
}

int output_multi_t::node_delete(osmid_t id) {
    if (m_processor->interests(geometry_processor::interest_node)) {
        // TODO - need to know it's a node?
        delete_from_output(id);
    }
    return 0;
}

int output_multi_t::way_delete(osmid_t id) {
    if (m_processor->interests(geometry_processor::interest_way)) {
        // TODO - need to know it's a way?
        delete_from_output(id);
    }
    return 0;
}

int output_multi_t::relation_delete(osmid_t id) {
    if (m_processor->interests(geometry_processor::interest_relation)) {
        // TODO - need to know it's a relation?
        delete_from_output(id);
    }
    return 0;
}

int output_multi_t::process_node(osmid_t id, double lat, double lon, struct keyval *tags) {
    //check if we are keeping this node
    unsigned int filter = m_tagtransform->filter_node_tags(tags, m_export_list.get());
    if (!filter) {
        //grab its geom
        geometry_builder::maybe_wkt_t wkt = m_processor->process_node(lat, lon);
        if (wkt) {
            copy_to_table(id, wkt->geom.c_str(), tags);
        }
    }
    return 0;
}

int output_multi_t::process_way(osmid_t id, const osmid_t *nodes, int node_count, struct keyval *tags) {
    //check if we are keeping this way
    int polygon = 0, roads = 0;
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get());
    if (!filter) {
        //grab its geom
        geometry_builder::maybe_wkt_t wkt = m_processor->process_way(nodes, node_count, m_mid);
        if (wkt) {
            //if we are also interested in relations we need to mark
            //this way pending just in case it shows up in one
            if (m_processor->interests(geometry_processor::interest_relation)) {
                ways_pending_tracker->mark(id);
            }//we aren't interested in relations so if it comes in on a relation later we wont keep it
            else {
                copy_to_table(id, wkt->geom.c_str(), tags);
            }
        }
    }
    return 0;
}

output_multi_t::member_helper::member_helper():members(NULL), member_count(0), way_count(0) {

}
output_multi_t::member_helper::~member_helper() {
    //clean up
    for(size_t i = 0; i < way_count; ++i)
    {
        resetList(&(tags[i]));
        free(nodes[i]);
    }
}

void output_multi_t::member_helper::set(const member* member_list, const int member_list_length, const middle_query_t* mid) {
    //clean up
    for(size_t i = 0; i < way_count; ++i)
    {
        resetList(&(tags[i]));
        free(nodes[i]);
    }

    //keep a few things
    members = member_list;
    member_count = member_list_length;

    //grab the way members' ids
    way_ids.resize(member_count);
    size_t used = 0;
    for(size_t i = 0; i < member_count; ++i)
        if(members[i].type == OSMTYPE_WAY)
            way_ids[used++] = members[i].id;

    //if we didnt end up using any well bail
    if(used == 0)
    {
        way_count = 0;
        return;
    }

    //get the nodes of the ways
    tags.resize(used + 1);
    node_counts.resize(used + 1);
    nodes.resize(used + 1);
    ways.resize(used + 1);
    //this is mildly abusive treating vectors like arrays but the memory is contiguous so...
    way_count = mid->ways_get_list(&way_ids.front(), used, &ways.front(), &tags.front(), &nodes.front(), &node_counts.front());

    //grab the roles of each way
    roles.resize(way_count + 1);
    roles[way_count] = NULL;
    for (size_t i = 0; i < way_count; ++i)
    {
        size_t j = i;
        for (; j < member_count; ++j)
        {
            if (members[j].id == ways[i])
            {
                break;
            }
        }
        roles[i] = members[j].role;
    }

    //mark the ends of each so whoever uses them will know where they end..
    nodes[way_count] = NULL;
    node_counts[way_count] = 0;
    ways[way_count] = 0;
}


int output_multi_t::process_relation(osmid_t id, const member *members, int member_count, struct keyval *tags) {
    //check if we are keeping this relation
    unsigned int filter = m_tagtransform->filter_rel_tags(tags, m_export_list.get());
    if (!filter) {
        //grab ways nodes about the members in the relation
        m_member_helper.set(members, member_count, m_mid);
        //figure out which members we are going to keep with the relation or not
        /*std::vector<int> members_superseeded(member_count);
        if (m_tagtransform->filter_rel_member_tags(tags, member_count, xtags, xrole, &members_superseeded.front(), &make_boundary, &make_polygon, &roads, m_export_list)) {
            return 0;
        }*/


        //TODO: do another level of filtering to get the members that end up belonging to the outer ring
        //by way of their tags being the same
        //then if they did end up belonging we need to mark them in ways_done_tracker->mark(xid[i]);
        //and remove them if for some reason they were already written, dont understand how they could be though
        //given that they go to pending first...
        //also we may want to do some of this work inside of process relation by pasing the tag transform to there
        //and by passing back some other things..

        geometry_builder::maybe_wkts_t wkts = m_processor->process_relation(members, member_count, m_mid);
        if (wkts) {
            for(geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt)
            {
                copy_to_table(id, wkt->geom.c_str(), tags);
            }
        }
    }
    return 0;
}

void output_multi_t::copy_to_table(osmid_t id, const char *wkt, struct keyval *tags) {
    m_table->write_wkt(id, tags, wkt);
}

void output_multi_t::delete_from_output(osmid_t id) {
    m_table->delete_row(id);
}

