/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Wildcard matching.

*/

/**
 * Case sensitive wild card match with a string.
 * * matches any string or no character.
 * ? matches any single character.
 * anything else etc must match the character exactly.
 *
 * Returns if a match was found.
 */
bool wildMatch(char const *first, char const *second)
{
    // Code borrowed from
    // http://www.geeksforgeeks.org/wildcard-character-matching/
    if (*first == '\0' && *second == '\0') {
        return true;
    }

    if (*first == '*' && *(first + 1) != '\0' && *second == '\0') {
        return false;
    }

    if (*first == '?' || *first == *second) {
        return wildMatch(first + 1, second + 1);
    }

    if (*first == '*') {
        return wildMatch(first + 1, second) || wildMatch(first, second + 1);
    }

    return false;
}
