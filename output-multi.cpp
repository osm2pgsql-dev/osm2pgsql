#include "output-multi.hpp"
#include "expire-tiles.hpp"
#include "id-tracker.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "table.hpp"
#include "taginfo_impl.hpp"
#include "tagtransform.hpp"
#include "wkb.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <vector>

output_multi_t::output_multi_t(const std::string &name,
                               std::shared_ptr<geometry_processor> processor_,
                               const struct export_list &export_list_,
                               const middle_query_t *mid_,
                               const options_t &options_)
: output_t(mid_, options_),
  m_tagtransform(tagtransform_t::make_tagtransform(&m_options)),
  m_export_list(new export_list(export_list_)), m_processor(processor_),
  m_proj(m_options.projection),
  // TODO: we could in fact have something that is interested in nodes and
  // ways..
  m_osm_type(m_processor->interests(geometry_processor::interest_node)
                 ? osmium::item_type::node
                 : osmium::item_type::way),
  m_table(new table_t(
      m_options.database_options.conninfo(), name, m_processor->column_type(),
      m_export_list->normal_columns(m_osm_type), m_options.hstore_columns,
      m_processor->srid(), m_options.append, m_options.slim, m_options.droptemp,
      m_options.hstore_mode, m_options.enable_hstore_index,
      m_options.tblsmain_data, m_options.tblsmain_index)),
  ways_done_tracker(new id_tracker()),
  m_expire(m_options.expire_tiles_zoom, m_options.expire_tiles_max_bbox,
           m_options.projection),
  buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  m_builder(m_options.projection, m_options.enable_multi),
  m_way_area(m_export_list->has_column(m_osm_type, "way_area"))
{
}

output_multi_t::output_multi_t(const output_multi_t &other)
: output_t(other.m_mid, other.m_options),
  m_tagtransform(tagtransform_t::make_tagtransform(&m_options)),
  m_export_list(new export_list(*other.m_export_list)),
  m_processor(other.m_processor), m_proj(other.m_proj),
  m_osm_type(other.m_osm_type), m_table(new table_t(*other.m_table)),
  // NOTE: we need to know which ways were used by relations so each thread
  // must have a copy of the original marked done ways, its read only so its
  // ok
  ways_done_tracker(other.ways_done_tracker),
  m_expire(m_options.expire_tiles_zoom, m_options.expire_tiles_max_bbox,
           m_options.projection),
  buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  m_builder(m_options.projection, m_options.enable_multi),
  m_way_area(other.m_way_area)
{
}

output_multi_t::~output_multi_t() = default;

std::shared_ptr<output_t> output_multi_t::clone(const middle_query_t* cloned_middle) const
{
    auto *clone = new output_multi_t(*this);
    clone->m_mid = cloned_middle;
    return std::shared_ptr<output_t>(clone);
}

int output_multi_t::start() {
    m_table->start();
    return 0;
}

size_t output_multi_t::pending_count() const {
    return ways_pending_tracker.size() + rels_pending_tracker.size();
}

void output_multi_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = ways_pending_tracker.last_returned();
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
    osmid_t popped = ways_pending_tracker.pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        if (!ways_done_tracker->is_marked(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
        popped = ways_pending_tracker.pop_mark();
    }

    //make sure to get this one as well and move to the next
    if (popped > id) {
        if (!ways_done_tracker->is_marked(popped) && id_tracker::is_valid(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
    }
}

int output_multi_t::pending_way(osmid_t id, int exists) {
    int ret = 0;

    // Try to fetch the way from the DB
    buffer.clear();
    if (m_mid->ways_get(id, buffer)) {
        // Output the way
        ret = reprocess_way(&buffer.get<osmium::Way>(0), exists);
    }

    return ret;
}

void output_multi_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
    osmid_t const prev = rels_pending_tracker.last_returned();
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
    osmid_t popped = rels_pending_tracker.pop_mark();
    if(!id_tracker::is_valid(popped))
        return;

    //get all the ones up to the id that was passed in
    while (popped < id) {
        job_queue.push(pending_job_t(popped, output_id));
        added++;
        popped = rels_pending_tracker.pop_mark();
    }

    //make sure to get this one as well and move to the next
    if (popped > id) {
        if(id_tracker::is_valid(popped)) {
            job_queue.push(pending_job_t(popped, output_id));
            added++;
        }
    }
}

int output_multi_t::pending_relation(osmid_t id, int exists) {
    int ret = 0;

    // Try to fetch the relation from the DB
    buffer.clear();
    if (m_mid->relations_get(id, buffer)) {
        auto const &rel = buffer.get<osmium::Relation>(0);
        ret = process_relation(rel, exists, true);
    }

    return ret;
}

void output_multi_t::stop(osmium::thread::Pool *pool)
{
    pool->submit(std::bind(&table_t::stop, m_table.get()));
    if (m_options.expire_tiles_zoom_min > 0) {
        m_expire.output_and_destroy(m_options.expire_tiles_filename.c_str(),
                                    m_options.expire_tiles_zoom_min);
    }
}

void output_multi_t::commit() {
    m_table->commit();
}

int output_multi_t::node_add(osmium::Node const &node)
{
    if (m_processor->interests(geometry_processor::interest_node)) {
        return process_node(node);
    }
    return 0;
}

int output_multi_t::way_add(osmium::Way *way) {
    if (m_processor->interests(geometry_processor::interest_way) && way->nodes().size() > 1) {
        return process_way(way);
    }
    return 0;
}


int output_multi_t::relation_add(osmium::Relation const &rel) {
    if (m_processor->interests(geometry_processor::interest_relation)
        && !rel.members().empty()) {
        return process_relation(rel, 0);
    }
    return 0;
}

int output_multi_t::node_modify(osmium::Node const &node)
{
    if (m_processor->interests(geometry_processor::interest_node)) {
        // TODO - need to know it's a node?
        delete_from_output(node.id());

        // TODO: need to mark any ways or relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_node(node);
    }

    return 0;
}

int output_multi_t::way_modify(osmium::Way *way) {
    if (m_processor->interests(geometry_processor::interest_way)) {
        // TODO - need to know it's a way?
        delete_from_output(way->id());

        // TODO: need to mark any relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_way(way);
    }

    return 0;
}

int output_multi_t::relation_modify(osmium::Relation const &rel) {
    if (m_processor->interests(geometry_processor::interest_relation)) {
        // TODO - need to know it's a relation?
        delete_from_output(-rel.id());

        // TODO: need to mark any other relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        return process_relation(rel, false);

    }

    return 0;
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

int output_multi_t::process_node(osmium::Node const &node)
{
    // check if we are keeping this node
    taglist_t outtags;
    auto filter = m_tagtransform->filter_tags(node, 0, 0, *m_export_list.get(),
                                              outtags, true);
    if (!filter) {
        // grab its geom
        auto geom = m_processor->process_node(node.location(), &m_builder);
        if (!geom.empty()) {
            m_expire.from_wkb(geom.c_str(), node.id());
            copy_node_to_table(node.id(), geom, outtags);
        }
    }
    return 0;
}

int output_multi_t::reprocess_way(osmium::Way *way, bool exists)
{
    //if the way could exist already we have to make the relation pending and reprocess it later
    //but only if we actually care about relations
    if(m_processor->interests(geometry_processor::interest_relation) && exists) {
        way_delete(way->id());
        const std::vector<osmid_t> rel_ids =
            m_mid->relations_using_way(way->id());
        for (std::vector<osmid_t>::const_iterator itr = rel_ids.begin(); itr != rel_ids.end(); ++itr) {
            rels_pending_tracker.mark(*itr);
        }
    }

    //check if we are keeping this way
    taglist_t outtags;
    unsigned int filter = m_tagtransform->filter_tags(
        *way, 0, 0, *m_export_list.get(), outtags, true);
    if (!filter) {
        m_mid->nodes_get_list(&(way->nodes()));
        auto geom = m_processor->process_way(*way, &m_builder);
        if (!geom.empty()) {
            copy_to_table(way->id(), geom, outtags);
        }
    }
    return 0;
}

int output_multi_t::process_way(osmium::Way *way) {
    //check if we are keeping this way
    taglist_t outtags;
    auto filter = m_tagtransform->filter_tags(*way, 0, 0, *m_export_list.get(), outtags, true);
    if (!filter) {
        //get the geom from the middle
        if (m_mid->nodes_get_list(&(way->nodes())) < 1)
            return 0;
        //grab its geom
        auto geom = m_processor->process_way(*way, &m_builder);

        if (!geom.empty()) {
            //if we are also interested in relations we need to mark
            //this way pending just in case it shows up in one
            if (m_processor->interests(geometry_processor::interest_relation)) {
                ways_pending_tracker.mark(way->id());
            } else {
                // We wouldn't be interested in this as a relation, so no need to mark it pending.
                // TODO: Does this imply anything for non-multipolygon relations?
                copy_to_table(way->id(), geom, outtags);
            }
        }
    }
    return 0;
}


int output_multi_t::process_relation(osmium::Relation const &rel,
                                     bool exists, bool pending)
{
    //if it may exist already, delete it first
    if(exists)
        relation_delete(rel.id());

    //does this relation have anything interesting to us
    taglist_t rel_outtags;
    auto filter = m_tagtransform->filter_tags(rel, 0, 0, *m_export_list.get(),
                                              rel_outtags, true);
    if (!filter) {
        //TODO: move this into geometry processor, figure a way to come back for tag transform
        //grab ways/nodes of the members in the relation, bail if none were used
        if (m_relation_helper.set(rel, (middle_t *)m_mid) < 1)
            return 0;

        //NOTE: make_polygon is preset here this is to force the tag matching/superseded stuff
        //normally this wouldnt work but we tell the tag transform to allow typeless relations
        //this is needed because the type can get stripped off by the rel_tag filter above
        //if the export list did not include the type tag.
        //TODO: find a less hacky way to do the matching/superseded and tag copying stuff without
        //all this trickery
        int roads;
        int make_boundary, make_polygon;
        taglist_t outtags;
        filter = m_tagtransform->filter_rel_member_tags(
            rel_outtags, m_relation_helper.data, m_relation_helper.roles,
            &m_relation_helper.superseded.front(), &make_boundary,
            &make_polygon, &roads, *m_export_list.get(), outtags, true);
        if (!filter)
        {
            m_relation_helper.add_way_locations((middle_t *)m_mid);
            auto geoms = m_processor->process_relation(
                rel, m_relation_helper.data, &m_builder);
            for (const auto geom : geoms) {
                copy_to_table(-rel.id(), geom, outtags);
            }

            //TODO: should this loop be inside the if above just in case?
            //take a look at each member to see if its superseded (tags on it matched the tags on the relation)
            size_t i = 0;
            for (auto const &w : m_relation_helper.data.select<osmium::Way>()) {
                //tags matched so we are keeping this one with this relation
                if (m_relation_helper.superseded[i]) {
                    //just in case it wasnt previously with this relation we get rid of them
                    way_delete(w.id());
                    //the other option is that we marked them pending in the way processing so here we mark them
                    //done so when we go back over the pendings we can just skip it because its in the done list
                    //TODO: dont do this when working with pending relations to avoid thread races
                    if(!pending)
                        ways_done_tracker->mark(w.id());
                }
                ++i;
            }
        }
    }
    return 0;
}

void output_multi_t::copy_node_to_table(osmid_t id, std::string const &geom,
                                        taglist_t &tags)
{
    m_table->write_row(id, tags, geom);
}

/**
 * Copies a 2d object(line or polygon) to the table, adding a way_area tag if appropriate
 * \param id OSM ID of the object
 * \param geom Geometry string of the object
 * \param tags List of tags. May be modified.
 * \param polygon Polygon flag returned from the tag transform (polygon=1)
 *
 * \pre geom must be valid.
 */
void output_multi_t::copy_to_table(const osmid_t id,
                                   geometry_processor::wkb_t const &geom,
                                   taglist_t &tags)
{
    // XXX really should depend on expected output type
    if (m_way_area) {
        // It's a polygon table (implied by it turning into a poly),
        // and it got formed into a polygon, so expire as a polygon and write the geom
        auto area =
            ewkb::parser_t(geom).get_area<osmium::geom::IdentityProjection>();
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%g", area);
        tags.push_override(tag_t("way_area", tmp));
    }

    m_expire.from_wkb(geom.c_str(), id);
    m_table->write_row(id, tags, geom);
}

void output_multi_t::delete_from_output(osmid_t id) {
    if(m_expire.from_db(m_table.get(), id))
        m_table->delete_row(id);
}

void output_multi_t::merge_pending_relations(output_t *other)
{
    auto *omulti = dynamic_cast<output_multi_t *>(other);

    if (omulti) {
        osmid_t id;
        while (id_tracker::is_valid((id = omulti->rels_pending_tracker.pop_mark()))) {
            rels_pending_tracker.mark(id);
        }
    }
}

void output_multi_t::merge_expire_trees(output_t *other)
{
    auto *omulti = dynamic_cast<output_multi_t *>(other);

    if (omulti) {
        m_expire.merge_and_destroy(omulti->m_expire);
    }
}
