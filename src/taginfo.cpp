/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2022 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "format.hpp"
#include "logging.hpp"
#include "taginfo-impl.hpp"

#include <cerrno>
#include <cstring>
#include <map>
#include <stdexcept>
#include <system_error>

#include <osmium/util/string.hpp>

void export_list::add(osmium::item_type type, taginfo const &info)
{
    m_export_list(type).push_back(info);
}

std::vector<taginfo> const &export_list::get(osmium::item_type type) const
    noexcept
{
    return m_export_list(type);
}

columns_t export_list::normal_columns(osmium::item_type type) const
{
    columns_t columns;

    for (auto const &info : m_export_list(type)) {
        if (!(info.flags & (FLAG_DELETE | FLAG_NOCOLUMN))) {
            columns.emplace_back(info.name, info.type, info.column_type());
        }
    }

    return columns;
}

unsigned parse_tag_flags(std::string const &flags, int lineno)
{
    static std::map<std::string, unsigned> const tagflags = {
        {"polygon", FLAG_POLYGON}, {"linear", FLAG_LINEAR},
        {"nocache", FLAG_NOCACHE}, {"delete", FLAG_DELETE},
        {"phstore", FLAG_PHSTORE}, {"nocolumn", FLAG_NOCOLUMN}};

    unsigned temp_flags = 0;

    for (auto const &flag_name : osmium::split_string(flags, ",\r\n")) {
        auto const it = tagflags.find(flag_name);
        if (it != tagflags.end()) {
            temp_flags |= it->second;
        } else {
            log_warn("Unknown flag '{}' line {}, ignored", flag_name, lineno);
        }
    }

    return temp_flags;
}

/**
 * Get the tag type. For unknown types, 0 will be returned.
 */
static unsigned get_tag_type(std::string const &tag)
{
    static std::map<std::string, unsigned> const tagtypes = {
        {"smallint", FLAG_INT_TYPE}, {"integer", FLAG_INT_TYPE},
        {"bigint", FLAG_INT_TYPE},   {"int2", FLAG_INT_TYPE},
        {"int4", FLAG_INT_TYPE},     {"int8", FLAG_INT_TYPE},
        {"real", FLAG_REAL_TYPE},    {"double precision", FLAG_REAL_TYPE}};

    auto const typ = tagtypes.find(tag);
    if (typ != tagtypes.end()) {
        return typ->second;
    }

    return 0;
}

bool read_style_file(std::string const &filename, export_list *exlist)
{
    bool enable_way_area = true;

    FILE *const in = std::fopen(filename.c_str(), "rt");
    if (!in) {
        throw std::system_error{
            errno, std::system_category(),
            "Couldn't open style file '{}'"_format(filename)};
    }

    char buffer[1024];
    int lineno = 0;
    bool read_valid_column = false;
    //for each line of the style file
    while (std::fgets(buffer, sizeof(buffer), in) != nullptr) {
        ++lineno;

        //find where a comment starts and terminate the string there
        char *const str = std::strchr(buffer, '#');
        if (str) {
            *str = '\0';
        }

        //grab the expected fields for this row
        char osmtype[24] = {0};
        char tag[64] = {0};
        char datatype[24] = {0};
        char flags[128] = {0};
        int const fields = std::sscanf(buffer, "%23s %63s %23s %127s", osmtype,
                                       tag, datatype, flags);
        if (fields <= 0) { /* Blank line */
            continue;
        }

        if (fields < 3) {
            std::fclose(in);
            throw std::runtime_error{
                "Error reading style file line {} (fields={})."_format(lineno,
                                                                       fields)};
        }

        //place to keep info about this tag
        taginfo temp{};
        temp.name = tag;
        temp.type = datatype;
        temp.flags = parse_tag_flags(flags, lineno);

        // check for special data types, by default everything is handled as text
        //
        // Ignore the special way_area column. It is of type real but we don't really
        // want to convert it back and forth between string and real later. The code
        // will provide a string suitable for the database already.
        if (temp.name != "way_area") {
            temp.flags |= get_tag_type(temp.type);
        }

        if ((temp.flags != FLAG_DELETE) &&
            ((temp.name.find('?') != std::string::npos) ||
             (temp.name.find('*') != std::string::npos))) {
            std::fclose(in);
            throw std::runtime_error{
                "Wildcard '{}' in non-delete style entry."_format(temp.name)};
        }

        if ((temp.name == "way_area") && (temp.flags == FLAG_DELETE)) {
            enable_way_area = false;
        }

        bool kept = false;

        //keep this tag info if it applies to nodes
        if (std::strstr(osmtype, "node")) {
            exlist->add(osmium::item_type::node, temp);
            kept = true;
        }

        //keep this tag info if it applies to ways
        if (std::strstr(osmtype, "way")) {
            exlist->add(osmium::item_type::way, temp);
            kept = true;
        }

        //do we really want to completely quit on an unusable line?
        if (!kept) {
            std::fclose(in);
            throw std::runtime_error{
                "Weird style line {}:{}."_format(filename, lineno)};
        }

        read_valid_column = true;
    }

    if (std::ferror(in)) {
        int const err = errno;
        std::fclose(in);
        throw std::system_error{
            err, std::system_category(),
            "Error reading style file '{}'"_format(filename)};
    }

    std::fclose(in);

    if (!read_valid_column) {
        throw std::runtime_error{"Unable to parse any valid columns from "
                                 "the style file. Aborting."};
    }

    return enable_way_area;
}
