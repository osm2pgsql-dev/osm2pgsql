#include "output-multi.hpp"
#include "taginfo_impl.hpp"
#include "table.hpp"
#include "tagtransform.hpp"
#include "options.hpp"
#include "middle.hpp"
#include "id-tracker.hpp"
#include "geometry-builder.hpp"
#include "expire-tiles.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <vector>

output_multi_t::output_multi_t(const std::string &name,
                               std::shared_ptr<geometry_processor> processor_,
                               const struct export_list &export_list_,
                               const middle_query_t* mid_, const options_t &options_)
    : output_t(mid_, options_),
      m_tagtransform(new tagtransform(&m_options)),
      m_export_list(new export_list(export_list_)),
      m_processor(processor_),
      //TODO: we could in fact have something that is interested in nodes and ways..
      m_osm_type(m_processor->interests(geometry_processor::interest_node) ? OSMTYPE_NODE : OSMTYPE_WAY),
      m_table(new table_t(m_options.database_options.conninfo(), name, m_processor->column_type(),
                          m_export_list->normal_columns(m_osm_type),
                          m_options.hstore_columns, m_processor->srid(),
                          m_options.append, m_options.slim, m_options.droptemp,
                          m_options.hstore_mode, m_options.enable_hstore_index,
                          m_options.tblsmain_data, m_options.tblsmain_index)),
      ways_pending_tracker(new id_tracker()), ways_done_tracker(new id_tracker()), rels_pending_tracker(new id_tracker()),
      m_expire(new expire_tiles(&m_options)) {
}

output_multi_t::output_multi_t(const output_multi_t& other):
    output_t(other.m_mid, other.m_options), m_tagtransform(new tagtransform(&m_options)), m_export_list(new export_list(*other.m_export_list)),
    m_processor(other.m_processor), m_osm_type(other.m_osm_type), m_table(new table_t(*other.m_table)),
    ways_pending_tracker(new id_tracker()), ways_done_tracker(new id_tracker()), rels_pending_tracker(new id_tracker()),
    m_expire(new expire_tiles(&m_options)) {
}


output_multi_t::~output_multi_t() {
}

std::shared_ptr<output_t> output_multi_t::clone(const middle_query_t* cloned_middle) const{
    output_multi_t *clone = new output_multi_t(*this);
    clone->m_mid = cloned_middle;
    //NOTE: we need to know which ways were used by relations so each thread
    //must have a copy of the original marked done ways, its read only so its ok
    clone->ways_done_tracker = ways_done_tracker;
    return std::shared_ptr<output_t>(clone);
}

int output_multi_t::start() {
    m_table->start();
    return 0;
}

size_t output_multi_t::pending_count() const {
    return ways_pending_tracker->size() + rels_pending_tracker->size();
}

void output_multi_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = ways_pending_tracker->last_returned();
    if (id_tracker::is_valid(prev) && prev >= id) {
        if (prev > id) {
            job_queue.push(pending_job_t(id, output_id));
        }
        // already done the job
        return;
    }

    //make sure we get the one passed in
    if(!ways_done_tracker->is_marked(id) && id_tracker::is_valid(id)) {
        job_queue.push(pending_job_t(id, output_id));
        added++;
    }

    //grab the first one or bail if its not valid
    osmid_t popped = ways_pending_tracker->pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        if (!ways_done_tracker->is_marked(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
        popped = ways_pending_tracker->pop_mark();
    }

    //make sure to get this one as well and move to the next
    if(popped == id) {
        if (!ways_done_tracker->is_marked(popped) && id_tracker::is_valid(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
    }
}

int output_multi_t::pending_way(osmid_t id, int exists) {
    taglist_t tags_int;
    nodelist_t nodes_int;
    int ret = 0;

    // Try to fetch the way from the DB
    if (m_mid->ways_get(id, tags_int, nodes_int)) {
        // Output the way
        ret = reprocess_way(id, nodes_int, tags_int, exists);
    }

    return ret;
}

void output_multi_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = rels_pending_tracker->last_returned();
    if (id_tracker::is_valid(prev) && prev >= id) {
        if (prev > id) {
            job_queue.push(pending_job_t(id, output_id));
        }
        // already done the job
        return;
    }

    //make sure we get the one passed in
    if(id_tracker::is_valid(id)) {
        job_queue.push(pending_job_t(id, output_id));
        added++;
    }

    //grab the first one or bail if its not valid
    osmid_t popped = rels_pending_tracker->pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        job_queue.push(pending_job_t(popped, output_id));
        added++;
        popped = rels_pending_tracker->pop_mark();
    }

    //make sure to get this one as well and move to the next
    if(popped == id) {
        if(id_tracker::is_valid(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
    }
}

int output_multi_t::pending_relation(osmid_t id, int exists) {
    taglist_t tags_int;
    memberlist_t members_int;
    int ret = 0;

    // Try to fetch the relation from the DB
    if (m_mid->relations_get(id, members_int, tags_int)) {
        ret = process_relation(id, members_int, tags_int, exists);
    }

    return ret;
}

void output_multi_t::stop() {
    m_table->stop();
    m_expire->output_and_destroy();
    m_expire.reset();
}

void output_multi_t::commit() {
    m_table->commit();
}

int output_multi_t::node_add(osmid_t id, double lat, double lon, const taglist_t &tags) {
    if (m_processor->interests(geometry_processor::interest_node)) {
        return process_node(id, lat, lon, tags);
    }
    return 0;
}

int output_multi_t::way_add(osmid_t id, const idlist_t &nodes, const taglist_t &tags) {
    if (m_processor->interests(geometry_processor::interest_way) && nodes.size() > 1) {
        return process_way(id, nodes, tags);
    }
    return 0;
}


int output_multi_t::relation_add(osmid_t id, const memberlist_t &members, const taglist_t &tags) {
    if (m_processor->interests(geometry_processor::interest_relation) && !members.empty()) {
        return process_relation(id, members, tags, 0);
    }
    return 0;
}

int output_multi_t::node_modify(osmid_t id, double lat, double lon, const taglist_t &tags) {
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

int output_multi_t::way_modify(osmid_t id, const idlist_t &nodes, const taglist_t &tags) {
    if (m_processor->interests(geometry_processor::interest_way)) {
        // TODO - need to know it's a way?
        delete_from_output(id);

        // TODO: need to mark any relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_way(id, nodes, tags);

    } else {
        return 0;
    }
}

int output_multi_t::relation_modify(osmid_t id, const memberlist_t &members, const taglist_t &tags) {
    if (m_processor->interests(geometry_processor::interest_relation)) {
        // TODO - need to know it's a relation?
        delete_from_output(-id);

        // TODO: need to mark any other relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_relation(id, members, tags, false);

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

int output_multi_t::process_node(osmid_t id, double lat, double lon, const taglist_t &tags) {
    //check if we are keeping this node
    taglist_t outtags;
    unsigned int filter = m_tagtransform->filter_node_tags(tags, *m_export_list.get(), outtags, true);
    if (!filter) {
        //grab its geom
        auto geom = m_processor->process_node(lat, lon);
        if (geom.valid()) {
            m_expire->from_bbox(lon, lat, lon, lat);
            copy_node_to_table(id, geom.geom, outtags);
        }
    }
    return 0;
}

int output_multi_t::reprocess_way(osmid_t id, const nodelist_t &nodes, const taglist_t &tags, bool exists)
{
    //if the way could exist already we have to make the relation pending and reprocess it later
    //but only if we actually care about relations
    if(m_processor->interests(geometry_processor::interest_relation) && exists) {
        way_delete(id);
        const std::vector<osmid_t> rel_ids = m_mid->relations_using_way(id);
        for (std::vector<osmid_t>::const_iterator itr = rel_ids.begin(); itr != rel_ids.end(); ++itr) {
            rels_pending_tracker->mark(*itr);
        }
    }

    //check if we are keeping this way
    int polygon = 0, roads = 0;
    taglist_t outtags;
    unsigned int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads,
                                                          *m_export_list.get(), outtags, true);
    if (!filter) {
        //grab its geom
        auto geom = m_processor->process_way(nodes);
        if (geom.valid()) {
            copy_to_table(id, geom, outtags, polygon);
        }
    }
    return 0;
}

int output_multi_t::process_way(osmid_t id, const idlist_t &nodes, const taglist_t &tags) {
    //check if we are keeping this way
    int polygon = 0, roads = 0;
    taglist_t outtags;
    unsigned filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads,
                                                      *m_export_list.get(), outtags, true);
    if (!filter) {
        //get the geom from the middle
        if(m_way_helper.set(nodes, m_mid) < 1)
            return 0;
        //grab its geom
        auto geom = m_processor->process_way(m_way_helper.node_cache);

        if (geom.valid()) {
            //if we are also interested in relations we need to mark
            //this way pending just in case it shows up in one
            if (m_processor->interests(geometry_processor::interest_relation)) {
                ways_pending_tracker->mark(id);
            } else {
                // We wouldn't be interested in this as a relation, so no need to mark it pending.
                // TODO: Does this imply anything for non-multipolygon relations?
                copy_to_table(id, geom, outtags, polygon);
            }
        }
    }
    return 0;
}

int output_multi_t::process_relation(osmid_t id, const memberlist_t &members,
                                     const taglist_t &tags, bool exists, bool pending) {
    //if it may exist already, delete it first
    if(exists)
        relation_delete(id);

    //does this relation have anything interesting to us
    taglist_t rel_outtags;
    unsigned filter = m_tagtransform->filter_rel_tags(tags, *m_export_list.get(),
                                                      rel_outtags, true);
    if (!filter) {
        //TODO: move this into geometry processor, figure a way to come back for tag transform
        //grab ways/nodes of the members in the relation, bail if none were used
        if(m_relation_helper.set(&members, (middle_t*)m_mid) < 1)
            return 0;

        //filter the tags on each member because we got them from the middle
        //and since the middle is no longer tied to the output it no longer
        //shares any kind of tag transform and therefore has all original tags
        //so we filter here because each individual outputs cares about different tags
        int polygon, roads;
        multitaglist_t filtered(m_relation_helper.tags.size(), taglist_t());
        for(size_t i = 0; i < m_relation_helper.tags.size(); ++i)
        {
            m_tagtransform->filter_way_tags(m_relation_helper.tags[i], &polygon,
                                            &roads, *m_export_list.get(), filtered[i]);
            //TODO: if the filter says that this member is now not interesting we
            //should decrement the count and remove his nodes and tags etc. for
            //now we'll just keep him with no tags so he will get filtered later
        }

        //do the members of this relation have anything interesting to us
        //NOTE: make_polygon is preset here this is to force the tag matching/superseded stuff
        //normally this wouldnt work but we tell the tag transform to allow typeless relations
        //this is needed because the type can get stripped off by the rel_tag filter above
        //if the export list did not include the type tag.
        //TODO: find a less hacky way to do the matching/superseded and tag copying stuff without
        //all this trickery
        int make_boundary, make_polygon = 1;
        taglist_t outtags;
        filter = m_tagtransform->filter_rel_member_tags(rel_outtags, filtered, m_relation_helper.roles,
                                                        &m_relation_helper.superseeded.front(),
                                                        &make_boundary, &make_polygon, &roads,
                                                        *m_export_list.get(), outtags, true);
        if (!filter)
        {
            auto geoms = m_processor->process_relation(m_relation_helper.nodes);
            for (const auto geom: geoms) {
                //TODO: we actually have the nodes in the m_relation_helper and could use them
                //instead of having to reparse the wkb in the expiry code
                m_expire->from_wkb(geom.geom.c_str(), -id);
                //what part of the code relies on relation members getting negative ids?
                copy_to_table(-id, geom, outtags, make_polygon);
            }

            //TODO: should this loop be inside the if above just in case?
            //take a look at each member to see if its superseded (tags on it matched the tags on the relation)
            for(size_t i = 0; i < m_relation_helper.ways.size(); ++i) {
                //tags matched so we are keeping this one with this relation
                if (m_relation_helper.superseeded[i]) {
                    //just in case it wasnt previously with this relation we get rid of them
                    way_delete(m_relation_helper.ways[i]);
                    //the other option is that we marked them pending in the way processing so here we mark them
                    //done so when we go back over the pendings we can just skip it because its in the done list
                    //TODO: dont do this when working with pending relations to avoid thread races
                    if(!pending)
                        ways_done_tracker->mark(m_relation_helper.ways[i]);
                }
            }
        }
    }
    return 0;
}

void output_multi_t::copy_node_to_table(osmid_t id, const std::string &geom, const taglist_t &tags) {
    m_table->write_row(id, tags, geom);
}

/**
 * Copies a 2d object(line or polygon) to the table, adding a way_area tag if appropriate
 * \param id OSM ID of the object
 * \param geom Geometry string of the object
 * \param tags List of tags
 * \param polygon Polygon flag returned from the tag transform (polygon=1)
 *
 * \pre geom must be valid.
 */
void output_multi_t::copy_to_table(const osmid_t id, const geometry_builder::pg_geom_t &geom, const taglist_t &tags, int polygon) {
    if (geom.is_polygon()) {
        // It's a polygon table (implied by it turning into a poly),
        // and it got formed into a polygon, so expire as a polygon and write the geom
        m_expire->from_nodes_poly(m_way_helper.node_cache, id);
        m_table->write_row(id, tags, geom.geom);
    } else {
        // Linestring
        if (!polygon) {
            // non-polygons are okay
            m_expire->from_nodes_line(m_way_helper.node_cache);
            m_table->write_row(id, tags, geom.geom);
        }
    }
}

void output_multi_t::delete_from_output(osmid_t id) {
    if(m_expire->from_db(m_table.get(), id))
        m_table->delete_row(id);
}

void output_multi_t::merge_pending_relations(std::shared_ptr<output_t> other) {
    std::shared_ptr<id_tracker> tracker = other.get()->get_pending_relations();
    osmid_t id;
    while(tracker.get() && id_tracker::is_valid((id = tracker->pop_mark()))){
        rels_pending_tracker->mark(id);
    }
}

void output_multi_t::merge_expire_trees(std::shared_ptr<output_t> other) {
    if(other->get_expire_tree().get())
        m_expire->merge_and_destroy(*other.get()->get_expire_tree());
}

std::shared_ptr<id_tracker> output_multi_t::get_pending_relations() {
    return rels_pending_tracker;
}
std::shared_ptr<expire_tiles> output_multi_t::get_expire_tree() {
    return m_expire;
}
