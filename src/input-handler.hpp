#ifndef OSM2PGSQL_INPUT_HANDLER_HPP
#define OSM2PGSQL_INPUT_HANDLER_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * It contains the input_handler_t class.
 */

#include <osmium/fwd.hpp>
#include <osmium/handler.hpp>
#include <osmium/osm/box.hpp>

#include "progress-display.hpp"

class osmdata_t;

/**
 * When an OSM file is read, this handler is called for each node, way, and
 * relation. Depending on the processing mode (create or append), the type
 * of object and whether an object is added or deleted, the right functions
 * of the osmdata_t class are called.
 */
class input_handler_t : public osmium::handler::Handler
{
public:
    input_handler_t(osmium::Box const &bbox, bool append,
                    osmdata_t const *osmdata);

    void node(osmium::Node const &node);
    void way(osmium::Way &way);
    void relation(osmium::Relation const &rel);

    progress_display_t const &progress() const noexcept { return m_progress; }

private:
    void negative_id_warning();

    osmdata_t const *m_data;

    // Bounding box for node import (or invalid Box if everything should be
    // imported).
    osmium::Box m_bbox;

    // The progress meter will be updated as we go.
    progress_display_t m_progress;

    // Current type being parsed.
    osmium::item_type m_type = osmium::item_type::node;

    // Are we running in append mode?
    bool m_append;

    // Has a warning about a negative id already been issued?
    bool m_issued_warning_negative_id = false;
};

#endif // OSM2PGSQL_INPUT_HANDLER_HPP
