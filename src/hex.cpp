/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "hex.hpp"

#include <cassert>

namespace util {

void encode_hex(std::string const &in, std::string *out)
{
    assert(out);

    constexpr char const *const LOOKUP_HEX = "0123456789ABCDEF";

    for (auto const c : in) {
        unsigned int const num = static_cast<unsigned char>(c);
        (*out) += LOOKUP_HEX[(num >> 4U) & 0xfU];
        (*out) += LOOKUP_HEX[num & 0xfU];
    }
}

std::string encode_hex(std::string const &in)
{
    std::string result;
    result.reserve(in.size() * 2);
    encode_hex(in, &result);
    return result;
}

} // namespace util
