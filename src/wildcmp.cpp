/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

/* Wildcard matching.

*/

/**
 * Case sensitive wild card match with a string.
 * * matches any string or no character.
 * ? matches any single character.
 * anything else must match the character exactly.
 *
 * Returns if a match was found.
 */
bool wildMatch(char const *expr, char const *str) noexcept
{
    // Code based on
    // http://www.geeksforgeeks.org/wildcard-character-matching/
    if (*expr == '\0' && *str == '\0') {
        return true;
    }

    if (*expr == '*') {
        while (*(expr + 1) == '*') {
            ++expr;
        }
    }

    if (*expr == '*' && *(expr + 1) != '\0' && *str == '\0') {
        return false;
    }

    if (*expr == '?' || *expr == *str) {
        if (*str == '\0') {
            return false;
        }
        return wildMatch(expr + 1, str + 1);
    }

    if (*expr == '*') {
        return wildMatch(expr + 1, str) || wildMatch(expr, str + 1);
    }

    return false;
}
