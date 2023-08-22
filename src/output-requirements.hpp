#ifndef OSM2PGSQL_OUTPUT_REQUIREMENTS_HPP
#define OSM2PGSQL_OUTPUT_REQUIREMENTS_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/**
 * Outputs can signal their requirements to the middle by setting these fields.
 */
struct output_requirements
{
    /**
     * Need full node objects with tags, attributes (only if --extra-attributes
     * is set) and locations. If false, only node locations are needed.
     */
    bool full_nodes = false;

    /**
     * Need full way objects with tags, attributes (only if --extra-attributes
     * is set) and way nodes. If false, only way nodes are needed.
     */
    bool full_ways = false;

    /**
     * Need full relation objects with tags, attributes (only if
     * --extra-attributes is set) and members. If false, no data from relations
     * is needed.
     */
    bool full_relations = false;
};

#endif // OSM2PGSQL_OUTPUT_REQUIREMENTS_HPP
