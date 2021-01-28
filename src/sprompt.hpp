#ifndef OSM2PGSQL_SPROMPT_HPP
#define OSM2PGSQL_SPROMPT_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2021 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

char *simple_prompt(const char *prompt, int maxlen, int echo);

#endif // OSM2PGSQL_SPROMPT_HPP
