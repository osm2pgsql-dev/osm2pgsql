/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "hex.hpp"

#include <array>
#include <cassert>
#include <stdexcept>

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

namespace {

constexpr std::array<char, 256> const HEX_TABLE = {
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,
    0, 1, 2, 3,   4, 5, 6, 7,   8, 9, 0, 0,   0, 0, 0, 0,

    0, 10, 11, 12,   13, 14, 15, 0,   0, 0, 0, 0,   0, 0, 0, 0,
    0,  0,  0,  0,    0,  0,  0, 0,   0, 0, 0, 0,   0, 0, 0, 0,
    0, 10, 11, 12,   13, 14, 15, 0,   0, 0, 0, 0,   0, 0, 0, 0,
};

} // anonymous namespace

unsigned char decode_hex_char(char c) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    return HEX_TABLE[static_cast<std::size_t>(static_cast<unsigned char>(c))];
}

std::string decode_hex(std::string_view hex_string)
{
    if (hex_string.size() % 2 != 0) {
        throw std::runtime_error{"Invalid wkb: Not a valid hex string"};
    }

    std::string wkb;
    wkb.reserve(hex_string.size() / 2);

    // NOLINTNEXTLINE(llvm-qualified-auto, readability-qualified-auto)
    for (auto hex = hex_string.cbegin(); hex != hex_string.cend();) {
        unsigned int const c = decode_hex_char(*hex++);
        wkb += static_cast<char>((c << 4U) | decode_hex_char(*hex++));
    }

    return wkb;
}

} // namespace util
