#include "output-multi.hpp"
#include "expire-tiles.hpp"
#include "id-tracker.hpp"
#include "middle.hpp"
#include "options.hpp"
#include "table.hpp"
#include "taginfo-impl.hpp"
#include "tagtransform.hpp"
#include "wkb.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <vector>

output_multi_t::output_multi_t(
    std::string const &name, std::shared_ptr<geometry_processor> processor_,
    export_list const &export_list, std::shared_ptr<middle_query_t> const &mid,
    options_t const &options,
    std::shared_ptr<db_copy_thread_t> const &copy_thread)
: output_t(mid, options),
  m_tagtransform(tagtransform_t::make_tagtransform(&m_options, export_list)),
  m_processor(processor_), m_proj(m_options.projection),
  // TODO: we could in fact have something that is interested in nodes and
  // ways..
  m_osm_type(m_processor->interests(geometry_processor::interest_node)
                 ? osmium::item_type::node
                 : osmium::item_type::way),
  m_table(new table_t{name, m_processor->column_type(),
                      export_list.normal_columns(m_osm_type),
                      m_options.hstore_columns, m_processor->srid(),
                      m_options.append, m_options.hstore_mode, copy_thread,
                      m_options.output_dbschema}),
  m_expire(m_options.expire_tiles_zoom, m_options.expire_tiles_max_bbox,
           m_options.projection),
  buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  m_builder(m_options.projection),
  m_way_area(export_list.has_column(m_osm_type, "way_area"))
{}

output_multi_t::output_multi_t(
    output_multi_t const *other, std::shared_ptr<middle_query_t> const &mid,
    std::shared_ptr<db_copy_thread_t> const &copy_thread)
: output_t(mid, other->m_options),
  m_tagtransform(other->m_tagtransform->clone()),
  m_processor(other->m_processor), m_proj(other->m_proj),
  m_osm_type(other->m_osm_type),
  m_table(new table_t{*other->m_table, copy_thread}),
  m_expire(m_options.expire_tiles_zoom, m_options.expire_tiles_max_bbox,
           m_options.projection),
  buffer(1024, osmium::memory::Buffer::auto_grow::yes),
  m_builder(m_options.projection), m_way_area(other->m_way_area)
{}

output_multi_t::~output_multi_t() = default;

std::shared_ptr<output_t> output_multi_t::clone(
    std::shared_ptr<middle_query_t> const &mid,
    std::shared_ptr<db_copy_thread_t> const &copy_thread) const
{
    return std::shared_ptr<output_t>(
        new output_multi_t{this, mid, copy_thread});
}

void output_multi_t::start()
{
    m_table->start(m_options.database_options.conninfo(),
                   m_options.tblsmain_data);
}

void output_multi_t::pending_way(osmid_t id)
{
    // Try to fetch the way from the DB
    buffer.clear();
    if (m_mid->way_get(id, buffer)) {
        // Output the way
        reprocess_way(&buffer.get<osmium::Way>(0), true);
    }
}

void output_multi_t::pending_relation(osmid_t id)
{
    // Try to fetch the relation from the DB
    buffer.clear();
    if (m_mid->relation_get(id, buffer)) {
        auto const &rel = buffer.get<osmium::Relation>(0);
        process_relation(rel, true);
    }
}

void output_multi_t::stop(thread_pool_t *pool)
{
    pool->submit([this]() {
        m_table->stop(m_options.slim & !m_options.droptemp,
                      m_options.enable_hstore_index, m_options.tblsmain_index);
    });
    if (m_options.expire_tiles_zoom_min > 0) {
        m_expire.output_and_destroy(m_options.expire_tiles_filename.c_str(),
                                    m_options.expire_tiles_zoom_min);
    }
}

void output_multi_t::sync() { m_table->sync(); }

void output_multi_t::node_add(osmium::Node const &node)
{
    if (m_processor->interests(geometry_processor::interest_node)) {
        process_node(node);
    }
}

void output_multi_t::way_add(osmium::Way *way)
{
    if (m_processor->interests(geometry_processor::interest_way) &&
        way->nodes().size() > 1) {
        process_way(way);
    }
}

void output_multi_t::relation_add(osmium::Relation const &rel)
{
    if (m_processor->interests(geometry_processor::interest_relation) &&
        !rel.members().empty()) {
        process_relation(rel, false);
    }
}

void output_multi_t::node_modify(osmium::Node const &node)
{
    if (m_processor->interests(geometry_processor::interest_node)) {
        // TODO - need to know it's a node?
        delete_from_output(node.id());

        // TODO: need to mark any ways or relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        process_node(node);
    }
}

void output_multi_t::way_modify(osmium::Way *way)
{
    if (m_processor->interests(geometry_processor::interest_way)) {
        // TODO - need to know it's a way?
        delete_from_output(way->id());

        // TODO: need to mark any relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        process_way(way);
    }
}

void output_multi_t::relation_modify(osmium::Relation const &rel)
{
    if (m_processor->interests(geometry_processor::interest_relation)) {
        // TODO - need to know it's a relation?
        delete_from_output(-rel.id());

        // TODO: need to mark any other relations using it - depends on what
        // type of output this is... delegate to the geometry processor??
        process_relation(rel, false);
    }
}

void output_multi_t::node_delete(osmid_t id)
{
    if (m_processor->interests(geometry_processor::interest_node)) {
        // TODO - need to know it's a node?
        delete_from_output(id);
    }
}

void output_multi_t::way_delete(osmid_t id)
{
    if (m_processor->interests(geometry_processor::interest_way)) {
        // TODO - need to know it's a way?
        delete_from_output(id);
    }
}

void output_multi_t::relation_delete(osmid_t id)
{
    if (m_processor->interests(geometry_processor::interest_relation)) {
        // TODO - need to know it's a relation?
        delete_from_output(-id);
    }
}

void output_multi_t::process_node(osmium::Node const &node)
{
    // check if we are keeping this node
    taglist_t outtags;
    auto filter =
        m_tagtransform->filter_tags(node, nullptr, nullptr, outtags, true);
    if (!filter) {
        // grab its geom
        auto geom = m_processor->process_node(node.location(), &m_builder);
        if (!geom.empty()) {
            m_expire.from_wkb(geom.c_str(), node.id());
            copy_node_to_table(node.id(), geom, outtags);
        }
    }
}

void output_multi_t::reprocess_way(osmium::Way *way, bool exists)
{
    //if the way could exist already we have to make the relation pending and reprocess it later
    //but only if we actually care about relations
    if (m_processor->interests(geometry_processor::interest_relation) &&
        exists) {
        way_delete(way->id());
    }

    //check if we are keeping this way
    taglist_t outtags;
    auto const filter =
        m_tagtransform->filter_tags(*way, nullptr, nullptr, outtags, true);
    if (!filter) {
        m_mid->nodes_get_list(&(way->nodes()));
        auto geom = m_processor->process_way(*way, &m_builder);
        if (!geom.empty()) {
            copy_to_table(way->id(), geom, outtags);
        }
    }
}

void output_multi_t::process_way(osmium::Way *way)
{
    //check if we are keeping this way
    taglist_t outtags;
    auto const filter =
        m_tagtransform->filter_tags(*way, nullptr, nullptr, outtags, true);
    if (!filter) {
        //get the geom from the middle
        if (m_mid->nodes_get_list(&(way->nodes())) < 1) {
            return;
        }
        //grab its geom
        auto const geom = m_processor->process_way(*way, &m_builder);

        if (!geom.empty()) {
            copy_to_table(way->id(), geom, outtags);
        }
    }
}

void output_multi_t::process_relation(osmium::Relation const &rel, bool exists)
{
    //if it may exist already, delete it first
    if (exists) {
        relation_delete(rel.id());
    }

    //does this relation have anything interesting to us
    taglist_t rel_outtags;
    auto filter =
        m_tagtransform->filter_tags(rel, nullptr, nullptr, rel_outtags, true);
    if (!filter) {
        //TODO: move this into geometry processor, figure a way to come back for tag transform
        //grab ways/nodes of the members in the relation, bail if none were used
        if (m_relation_helper.set(rel, m_mid.get()) < 1) {
            return;
        }

        //NOTE: make_polygon is preset here this is to force the tag matching
        //normally this wouldnt work but we tell the tag transform to allow typeless relations
        //this is needed because the type can get stripped off by the rel_tag filter above
        //if the export list did not include the type tag.
        //TODO: find a less hacky way to do the matching and tag copying stuff without
        //all this trickery
        int roads;
        int make_boundary, make_polygon;
        taglist_t outtags;
        filter = m_tagtransform->filter_rel_member_tags(
            rel_outtags, m_relation_helper.data, m_relation_helper.roles,
            &make_boundary, &make_polygon, &roads, outtags, true);
        if (!filter) {
            m_relation_helper.add_way_locations(m_mid.get());
            auto const geoms = m_processor->process_relation(
                rel, m_relation_helper.data, &m_builder);
            for (auto const &geom : geoms) {
                copy_to_table(-rel.id(), geom, outtags);
            }
        }
    }
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
void output_multi_t::copy_to_table(osmid_t const id,
                                   geometry_processor::wkb_t const &geom,
                                   taglist_t &tags)
{
    // XXX really should depend on expected output type
    if (m_way_area) {
        // It's a polygon table (implied by it turning into a poly),
        // and it got formed into a polygon, so expire as a polygon and write the geom
        auto area =
            ewkb::parser_t(geom).get_area<osmium::geom::IdentityProjection>();
        util::double_to_buffer tmp{area};
        tags.set("way_area", tmp.c_str());
    }

    m_expire.from_wkb(geom.c_str(), id);
    m_table->write_row(id, tags, geom);
}

void output_multi_t::delete_from_output(osmid_t id)
{
    if (m_expire.from_db(m_table.get(), id)) {
        m_table->delete_row(id);
    }
}

void output_multi_t::merge_expire_trees(output_t *other)
{
    auto *const omulti = dynamic_cast<output_multi_t *>(other);

    if (omulti) {
        m_expire.merge_and_destroy(omulti->m_expire);
    }
}
