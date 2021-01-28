#ifndef OSM2PGSQL_PROCESSOR_POINT_HPP
#define OSM2PGSQL_PROCESSOR_POINT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "geometry-processor.hpp"

class processor_point : public geometry_processor
{
public:
    processor_point(std::shared_ptr<reprojection> const &proj);

    wkb_t process_node(osmium::Location const &loc,
                       geom::osmium_builder_t *builder) override;
};

#endif // OSM2PGSQL_PROCESSOR_POINT_HPP
