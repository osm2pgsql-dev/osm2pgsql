#include "output-multi.hpp"
#include "taginfo_impl.hpp"

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/algorithm/string/predicate.hpp>
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
      //TODO: we could in fact have something that is interested in nodes and ways..
      m_osm_type(m_processor->interests(geometry_processor::interest_node) ? OSMTYPE_NODE : OSMTYPE_WAY),
      m_table(new table_t(m_options.conninfo, mk_column_name(name, m_options), m_processor->column_type(),
                          m_export_list->normal_columns(m_osm_type),
                          m_options.hstore_columns, m_processor->srid(), m_options.scale,
                          m_options.append, m_options.slim, m_options.droptemp,
                          m_options.hstore_mode, m_options.enable_hstore_index,
                          m_options.tblsmain_data, m_options.tblsmain_index)),
      m_expire(new expire_tiles(&m_options)) {
}

output_multi_t::output_multi_t(const output_multi_t& other):
    output_t(other.m_mid, other.m_options), m_tagtransform(new tagtransform(&m_options)), m_export_list(new export_list(*other.m_export_list)),
    m_processor(geometry_processor::create(other.m_processor->column_type(), &m_options)), m_osm_type(other.m_osm_type), m_table(new table_t(*other.m_table)),
    m_expire(new expire_tiles(&m_options)) {
}


output_multi_t::~output_multi_t() {
}

boost::shared_ptr<output_t> output_multi_t::clone() {
    return boost::make_shared<output_multi_t>(*this);
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

int output_multi_t::way_cb_func::do_single(osmid_t id, int exists) {
    keyval tags_int;
    osmNode *nodes_int;
    int count_int;
    int ret = 0;

    // Check if it's marked as done
    if (!m_ptr->ways_done_tracker->is_marked(id)) {
        initList(&tags_int);
        // Try to fetch the way from the DB
        if (!m_ptr->m_mid->ways_get(id, &tags_int, &nodes_int, &count_int)) {
            // Output the way
            ret = m_ptr->reprocess_way(id,  nodes_int, count_int, &tags_int, exists);
            free(nodes_int);
        }
        resetList(&tags_int);
    }
    return 0;
}

int output_multi_t::way_cb_func::operator()(osmid_t id, int exists) {
    int ret = 0;
    //loop through the pending ways up to id
    while (m_next_internal_id < id) {
        ret = do_single(m_next_internal_id, exists) + ret > 0 ? 1 : 0;
        m_next_internal_id = m_ptr->ways_pending_tracker->pop_mark();
    }

    //make sure to get this one as well and move to the next
    ret = do_single(id, exists) + ret > 0 ? 1 : 0;
    if(m_next_internal_id == id) {
        m_next_internal_id = m_ptr->ways_pending_tracker->pop_mark();
    }

    //non zero is bad
    return ret;
}

void output_multi_t::way_cb_func::finish(int exists) {
    operator()(std::numeric_limits<osmid_t>::max(), exists);
}

output_multi_t::rel_cb_func::rel_cb_func(output_multi_t *ptr)
    : m_ptr(ptr), m_sql(),
      m_next_internal_id(m_ptr->rels_pending_tracker->pop_mark()) {
}

output_multi_t::rel_cb_func::~rel_cb_func() {}

int output_multi_t::rel_cb_func::do_single(osmid_t id, int exists) {
    keyval tags_int;
    member *members_int;
    int count_int;
    int ret = 0;
    initList(&tags_int);
    if (!m_ptr->m_mid->relations_get(id, &members_int, &count_int, &tags_int)) {
        ret = m_ptr->process_relation(id, members_int, count_int, &tags_int, exists);
        free(members_int);
    }
    resetList(&tags_int);
    return ret;
}

int output_multi_t::rel_cb_func::operator()(osmid_t id, int exists) {
    int ret = 0;

    //loop through the pending rels up to id
    while (m_next_internal_id < id) {
        ret = do_single(m_next_internal_id, exists) + ret > 0 ? 1 : 0;
        m_next_internal_id = m_ptr->rels_pending_tracker->pop_mark();
    }

    //make sure to get this one as well and move to the next
    ret = do_single(id, exists) + ret > 0 ? 1 : 0;
    if(m_next_internal_id == id) {
        m_next_internal_id = m_ptr->rels_pending_tracker->pop_mark();
    }

    //non zero is bad
    return ret;
}

void output_multi_t::rel_cb_func::finish(int exists) {
    operator()(std::numeric_limits<osmid_t>::max(), exists);
}

void output_multi_t::stop() {
    m_table->stop();
    m_expire.reset();
}

void output_multi_t::commit() {
    m_table->commit();
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
        delete_from_output(-id);

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
        delete_from_output(-id);
    }
    return 0;
}

int output_multi_t::process_node(osmid_t id, double lat, double lon, struct keyval *tags) {
    //check if we are keeping this node
    unsigned int filter = m_tagtransform->filter_node_tags(tags, m_export_list.get(), true);
    if (!filter) {
        //grab its geom
        geometry_builder::maybe_wkt_t wkt = m_processor->process_node(lat, lon);
        if (wkt) {
            m_expire->from_bbox(lon, lat, lon, lat);
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
        for (std::vector<osmid_t>::const_iterator itr = rel_ids.begin(); itr != rel_ids.end(); ++itr) {
            rels_pending_tracker->mark(*itr);
        }
    }

    //check if we are keeping this way
    int polygon = 0, roads = 0;
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get(), true);
    if (!filter) {
        //grab its geom
        geometry_builder::maybe_wkt_t wkt = m_processor->process_way(nodes, node_count);
        if (wkt) {
            //TODO: need to know if we care about polygons or lines for this output
            //the difference only being that if its a really large bbox for the poly
            //it downgrades to just invalidating the line/perimeter anyway
            if(boost::starts_with(wkt->geom, "POLYGON") || boost::starts_with(wkt->geom, "MULTIPOLYGON"))
                m_expire->from_nodes_poly(nodes, node_count, id);
            else
                m_expire->from_nodes_line(nodes, node_count);
            copy_to_table(id, wkt->geom.c_str(), tags);
        }
    }
    return 0;
}

int output_multi_t::process_way(osmid_t id, const osmid_t* node_ids, int node_count, struct keyval *tags) {
    //check if we are keeping this way
    int polygon = 0, roads = 0;
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list.get(), true);
    if (!filter) {
        //get the geom from the middle
        if(m_way_helper.set(node_ids, node_count, m_mid) < 1)
            return 0;
        //grab its geom
        geometry_builder::maybe_wkt_t wkt = m_processor->process_way(&m_way_helper.node_cache.front(), m_way_helper.node_cache.size());

        if (wkt) {
            //if we are also interested in relations we need to mark
            //this way pending just in case it shows up in one
            if (m_processor->interests(geometry_processor::interest_relation)) {
                ways_pending_tracker->mark(id);
            }//we aren't interested in relations so if it comes in on a relation later we wont keep it
            else {
                //TODO: need to know if we care about polygons or lines for this output
                //the difference only being that if its a really large bbox for the poly
                //it downgrades to just invalidating the line/perimeter anyway
                if(boost::starts_with(wkt->geom, "POLYGON") || boost::starts_with(wkt->geom, "MULTIPOLYGON"))
                    m_expire->from_nodes_poly(&m_way_helper.node_cache.front(), m_way_helper.node_cache.size(), id);
                else
                    m_expire->from_nodes_line(&m_way_helper.node_cache.front(), m_way_helper.node_cache.size());
                copy_to_table(id, wkt->geom.c_str(), tags);
            }
        }
    }
    return 0;
}

int output_multi_t::process_relation(osmid_t id, const member *members, int member_count, keyval *tags, bool exists) {
    //if it may exist already, delete it first
    if(exists)
        relation_delete(id);

    //does this relation have anything interesting to us
    unsigned int filter = m_tagtransform->filter_rel_tags(tags, m_export_list.get(), true);
    if (!filter) {
        //TODO: move this into geometry processor, figure a way to come back for tag transform
        //grab ways/nodes of the members in the relation, bail if none were used
        if(m_relation_helper.set(members, member_count, (middle_t*)m_mid) < 1)
            return 0;

        //filter the tags on each member because we got them from the middle
        //and since the middle is no longer tied to the output it no longer
        //shares any kind of tag transform and therefore has all original tags
        //so we filter here because each individual outputs cares about different tags
        int polygon, roads;
        for(size_t i = 0; i < m_relation_helper.way_count; ++i)
        {
            m_tagtransform->filter_way_tags(&m_relation_helper.tags[i], &polygon, &roads, m_export_list.get());
            //TODO: if the filter says that this member is now not interesting we
            //should decrement the count and remove his nodes and tags etc. for
            //now we'll just keep him with no tags so he will get filtered later
        }

        //do the members of this relation have anything interesting to us
        //NOTE: make_polygon is preset here this is to force the tag matching/superseeded stuff
        //normally this wouldnt work but we tell the tag transform to allow typeless relations
        //this is needed because the type can get stripped off by the rel_tag filter above
        //if the export list did not include the type tag.
        //TODO: find a less hacky way to do the matching/superseeded and tag copying stuff without
        //all this trickery
        int make_boundary, make_polygon = 1;
        filter = m_tagtransform->filter_rel_member_tags(tags, m_relation_helper.way_count, &m_relation_helper.tags.front(),
                                                   &m_relation_helper.roles.front(), &m_relation_helper.superseeded.front(),
                                                   &make_boundary, &make_polygon, &roads, m_export_list.get(), true);
        if(!filter)
        {
            geometry_builder::maybe_wkts_t wkts = m_processor->process_relation(&m_relation_helper.nodes.front(), &m_relation_helper.node_counts.front());
            if (wkts) {
                for(geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt)
                {
                    //TODO: we actually have the nodes in the m_relation_helper and could use them
                    //instead of having to reparse the wkt in the expiry code
                    m_expire->from_wkt(wkt->geom.c_str(), -id);
                    //what part of the code relies on relation members getting negative ids?
                    copy_to_table(-id, wkt->geom.c_str(), tags);
                }
            }

            //TODO: should this loop be inside the if above just in case?
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
    if(m_expire->from_db(m_table.get(), id))
        m_table->delete_row(id);
}

