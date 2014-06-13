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
                               const struct export_list &export_list_,
                               const middle_query_t* mid_, const options_t &options_) 
    : output_t(mid_, options_),
      m_tagtransform(new tagtransform(&m_options)),
      m_export_list(new export_list(export_list_)),
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
    /* To prevent deadlocks in parallel processing, the mid tables need
     * to stay out of a transaction. In this stage output tables are only
     * written to and not read, so they can be processed as several parallel
     * independent transactions
     */
    m_table->begin();

    /* Processing any remaing to be processed ways */
    way_cb_func *func = new way_cb_func(this);

    return func;
}

middle_t::rel_cb_func *output_multi_t::relation_callback() {
    /* Processing any remaing to be processed relations */
    /* During this stage output tables also need to stay out of
     * extended transactions, as the delete_way_from_output, called
     * from process_relation, can deadlock if using multi-processing.
     */
    rel_cb_func *rel_callback = new rel_cb_func(this);
    return rel_callback;
}

output_multi_t::way_cb_func::way_cb_func(output_multi_t *ptr)
    : m_ptr(ptr), m_sql(),
      m_next_internal_id(m_ptr->ways_pending_tracker->pop_mark()) {
}

output_multi_t::way_cb_func::~way_cb_func() {}

int output_multi_t::way_cb_func::operator()(osmid_t id, keyval *tags, const osmNode *nodes, int count, int exists) {
    if (m_next_internal_id < id) {
        run_internal_until(id, exists);
    }

    if (m_next_internal_id == id) {
        m_next_internal_id = m_ptr->ways_pending_tracker->pop_mark();
    }

    if (m_ptr->ways_done_tracker->is_marked(id)) {
        return 0;
    } else {
        return m_ptr->reprocess_way(id, nodes, count, tags, exists);
    }
}

void output_multi_t::way_cb_func::finish(int exists) {
    run_internal_until(std::numeric_limits<osmid_t>::max(), exists);
}

void output_multi_t::way_cb_func::run_internal_until(osmid_t id, int exists) {
    keyval tags_int;
    osmNode *nodes_int;
    int count_int;

    /* Loop through the pending ways up to id*/
    while (m_next_internal_id < id) {
        initList(&tags_int);
        /* Try to fetch the way from the DB */
        if (!m_ptr->m_mid->ways_get(m_next_internal_id, &tags_int, &nodes_int, &count_int)) {
            /* Check if it's marked as done */
            if (!m_ptr->ways_done_tracker->is_marked(m_next_internal_id)) {
                /* Output the way */
                m_ptr->reprocess_way(m_next_internal_id,  nodes_int, count_int, &tags_int, exists);
            }

            free(nodes_int);
        }
        resetList(&tags_int);

        m_next_internal_id = m_ptr->ways_pending_tracker->pop_mark();
    }
}

output_multi_t::rel_cb_func::rel_cb_func(output_multi_t *ptr)
    : m_ptr(ptr), m_sql(),
      m_next_internal_id(m_ptr->rels_pending_tracker->pop_mark()) {
}

output_multi_t::rel_cb_func::~rel_cb_func() {}

int output_multi_t::rel_cb_func::operator()(osmid_t id, const member *mems, int member_count, struct keyval *rel_tags, int exists) {
    if (m_next_internal_id < id) {
        run_internal_until(id, exists);
    }

    if (m_next_internal_id == id) {
        m_next_internal_id = m_ptr->rels_pending_tracker->pop_mark();
    }

    return m_ptr->process_relation(id, mems, member_count, rel_tags, exists);
}

void output_multi_t::rel_cb_func::finish(int exists) {
    run_internal_until(std::numeric_limits<osmid_t>::max(), exists);
}

void output_multi_t::rel_cb_func::run_internal_until(osmid_t id, int exists) {
    keyval tags_int;
    member *members_int;
    int count_int;

    while (m_next_internal_id < id) {
        initList(&tags_int);
        if (!m_ptr->m_mid->relations_get(m_next_internal_id, &members_int, &count_int, &tags_int)) {
            m_ptr->process_relation(m_next_internal_id, members_int, count_int, &tags_int, exists);

            free(members_int);
        }
        resetList(&tags_int);

        m_next_internal_id = m_ptr->rels_pending_tracker->pop_mark();
    }
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
        return process_relation(id, members, member_count, tags, 0);
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
        return process_relation(id, members, member_count, tags, false);

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

int output_multi_t::reprocess_way(osmid_t id, const osmNode* nodes, int node_count, struct keyval *tags, bool exists)
{
    //if the way could exist already we have to make the relation pending and reprocess it later
    //but only if we actually care about relations
    if(m_processor->interests(geometry_processor::interest_relation) && exists) {
        way_delete(id);
        // TODO: this now only has an effect when called from the iterate_ways
        // call-back, so we need some alternative way to trigger this within
        // osmdata_t.
        const std::vector<osmid_t> rel_ids = m_mid->relations_using_way(id);
        for (std::vector<osmid_t>::const_iterator itr = rel_ids.begin();
             itr != rel_ids.end(); ++itr) {
            rels_pending_tracker->mark(*itr);
        }
    }

    //check if we are keeping this way
    int polygon = 0, roads = 0;
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get());
    if (!filter) {
        //grab its geom
        geometry_builder::maybe_wkt_t wkt = m_processor->process_way(nodes, node_count);
        if (wkt) {
                copy_to_table(id, wkt->geom.c_str(), tags);
        }
    }
    return 0;
}

int output_multi_t::process_way(osmid_t id, const osmid_t* node_ids, int node_count, struct keyval *tags) {
    //check if we are keeping this way
    int polygon = 0, roads = 0;
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get());
    if (!filter) {
        //grab its geom
        geometry_builder::maybe_wkt_t wkt = m_processor->process_way(node_ids, node_count, m_mid);

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

int output_multi_t::process_relation(osmid_t id, const member *members, int member_count, struct keyval *tags, bool exists) {
    //if it may exist already, delete it first
    if(exists)
        relation_delete(id);

    //does this relation have anything interesting to us
    unsigned int filter = m_tagtransform->filter_rel_tags(tags, m_export_list.get());
    if (!filter) {
        //TODO: move this into geometry processor, figure a way to come back for tag transform
        //grab ways/nodes of the members in the relation, bail if none were used
        if(m_relation_helper.set(members, member_count, (middle_t*)m_mid) < 1)
            return 0;

        //do the members of this relation have anything interesting to us
        int make_boundary, make_polygon, roads;
        filter = m_tagtransform->filter_rel_member_tags(tags, m_relation_helper.way_count, &m_relation_helper.tags.front(),
                                                   &m_relation_helper.roles.front(), &m_relation_helper.superseeded.front(),
                                                   &make_boundary, &make_polygon, &roads, m_export_list.get());
        if(!filter)
        {
            geometry_builder::maybe_wkts_t wkts = m_processor->process_relation(&m_relation_helper.nodes.front(), &m_relation_helper.node_counts.front());
            if (wkts) {
                //TODO: expire
                for(geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt)
                {
                    //what part of the code relies on relation members getting negative ids?
                    copy_to_table(-id, wkt->geom.c_str(), tags);
                }
            }

            //take a look at each member to see if its superseeded (tags on it matched the tags on the relation)
            for(size_t i = 0; i < m_relation_helper.way_count; ++i) {
                //tags matched so we are keeping this one with this relation
                if (m_relation_helper.superseeded[i]) {
                    //just in case it wasnt previously with this relation we get rid of them
                    way_delete(m_relation_helper.ways[i]);
                    //the other option is that we marked them pending in the way processing so here we mark them
                    //done so when we go back over the pendings we can just skip it because its in the done list
                    ways_done_tracker->mark(m_relation_helper.ways[i]);
                }
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

