#ifndef OSM2PGSQL_HEX_HPP
#define OSM2PGSQL_HEX_HPP

/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <string>

namespace util {

/**
 * Convert content of input string to hex and append to output.
 *
 * \param in The input data.
 * \param out Pointer to output string.
 */
void encode_hex(std::string const &in, std::string *out);

/**
 * Convert content of input string to hex and return it.
 *
 * \param in The input data.
 * \returns Hex encoded string.
 */
[[nodiscard]] std::string encode_hex(std::string const &in);

/**
 * Decode one hex character (0-9A-F or 0-9a-f) and return its value.
 * Returns 0 for characters that are not hex characters.
 */
[[nodiscard]] unsigned char decode_hex_char(char c) noexcept;

/**
 * Decode a string of hex characters. Throws an exception if the input is not
 * a valid hex encoding.
 */
[[nodiscard]] std::string decode_hex(std::string_view hex);

} // namespace util

#endif // OSM2PGSQL_HEX_HPP
