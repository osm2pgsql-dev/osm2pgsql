#ifndef OSM2PGSQL_PROCESSOR_POLYGON_HPP
#define OSM2PGSQL_PROCESSOR_POLYGON_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2020 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geometry-processor.hpp"

class processor_polygon : public geometry_processor
{
public:
    processor_polygon(std::shared_ptr<reprojection> const &proj,
                      bool build_multigeoms);

    wkb_t process_way(osmium::Way const &way,
                      geom::osmium_builder_t *builder) override;
    wkbs_t process_relation(osmium::Relation const &rel,
                            osmium::memory::Buffer const &ways,
                            geom::osmium_builder_t *builder) override;

private:
    bool m_build_multigeoms;
};

#endif // OSM2PGSQL_PROCESSOR_POLYGON_HPP
