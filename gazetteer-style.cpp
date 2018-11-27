#include <algorithm>
#include <cstring>

#include <boost/property_tree/json_parser.hpp>
#include <osmium/osm.hpp>

#include "gazetteer-style.hpp"
#include "pgsql.hpp"
#include "wkb.hpp"

enum : int
{
    MAX_ADMINLEVEL = 15
};

namespace {
void escape_array_record(char const *in, std::string &out)
{
    for (char const *c = in; *c; ++c) {
        switch (*c) {
        case '\\':
            // Tripple escaping required: string escaping leaves us
            // with 4 backslashes, COPY then reduces it to two, which
            // are then interpreted as a single backslash by the hash
            // parsing code.
            out += "\\\\\\\\";
            break;
        case '\n':
        case '\r':
        case '\t':
        case '"':
            /* This is a bit naughty - we know that nominatim ignored these characters so just drop them now for simplicity */
            out += ' ';
            break;
        default:
            out += *c;
            break;
        }
    }
}

std::string domain_name(char const *cls, osmium::TagList const &tags)
{
    std::string ret;
    bool hasname = false;

    std::string prefix(cls);
    auto clen = prefix.length() + 1;
    prefix += ":name";
    auto plen = prefix.length();

    for (auto const &item : tags) {
        char const *k = item.key();
        if (prefix.compare(0, plen, k) == 0 &&
            (k[plen] == '\0' || k[plen] == ':')) {
            if (!hasname) {
                hasname = true;
            } else {
                ret += ",";
            }
            ret += "\"";
            escape_array_record(k + clen, ret);
            ret += "\"=>\"";
            escape_array_record(item.value(), ret);
            ret += "\"";
        }
    }

    return ret;
}
}

namespace pt = boost::property_tree;

void gazetteer_style_t::clear()
{
    m_main.clear();
    m_names.clear();
    m_extra.clear();
    m_address.clear();
    m_operator = nullptr;
    m_admin_level = MAX_ADMINLEVEL;
    m_is_named = false;
}

bool gazetteer_style_t::has_place(std::string const &cls) const
{
    return std::any_of(m_main.begin(), m_main.end(), [&](pmaintag_t const &e) {
        return strcmp(std::get<0>(e), cls.c_str()) == 0;
    });
}

void gazetteer_style_t::load_style(std::string const &filename)
{
    fprintf(stderr, "Parsing gazetteer style file.\n");
    pt::ptree root;

    pt::read_json(filename, root);

    for (auto &entry : root) {
        for (auto &tag : entry.second.get_child("keys")) {
            for (auto &value : entry.second.get_child("values")) {
                add_style_entry(tag.second.data(), value.first,
                                parse_flags(value.second.data()));
            }
        }
    }
}

gazetteer_style_t::flag_t gazetteer_style_t::parse_flags(std::string const &str)
{
    flag_t out = 0;

    std::string::size_type start = 0;

    while (start != std::string::npos) {
        auto end = str.find(',', start);

        std::string item;

        if (end == std::string::npos) {
            item = str.substr(start);
            start = std::string::npos;
        } else {
            item = str.substr(start, end - start);
            start = end + 1;
        }

        if (item == "skip") {
            return 0;
        } else if (item == "main") {
            out |= SF_MAIN;
        } else if (item == "with_name_key") {
            out |= SF_MAIN_NAMED_KEY;
        } else if (item == "with_name") {
            out |= SF_MAIN_NAMED;
        } else if (item == "fallback") {
            out |= SF_MAIN_FALLBACK;
        } else if (item == "operator") {
            out |= SF_MAIN_OPERATOR;
            m_any_operator_matches = true;
        } else if (item == "name") {
            out |= SF_NAME;
        } else if (item == "ref") {
            out |= SF_REF;
        } else if (item == "address") {
            out |= SF_ADDRESS;
        } else if (item == "house") {
            out |= SF_ADDRESS_POINT;
        } else if (item == "postcode") {
            out |= SF_POSTCODE;
        } else if (item == "country") {
            out |= SF_COUNTRY;
        } else if (item == "extra") {
            out |= SF_EXTRA;
        } else if (item == "interpolation") {
            out |= SF_INTERPOLATION;
        } else {
            throw std::runtime_error("Unknown flag in style file.");
        }
    }

    return out;
}

void gazetteer_style_t::add_style_entry(std::string const &key,
                                        std::string const &value,
                                        gazetteer_style_t::flag_t flags)
{
    if (key.empty()) {
        if (value.empty()) {
            m_default = flags;
        } else {
            m_matcher.emplace_back(value, flags, matcher_t::MT_VALUE);
        }
        return;
    }

    // prefix: works on empty key only
    if (key[key.size() - 1] == '*') {
        if (key.size() == 1) {
            throw std::runtime_error("Style error. Ambigious key '*'.");
        }
        if (!value.empty()) {
            throw std::runtime_error(
                "Style error. Prefix key can only be used with empty value.\n");
        }
        m_matcher.emplace_back(key.substr(0, key.size() - 1), flags,
                               matcher_t::MT_PREFIX);
        return;
    }

    // suffix: dito
    if (key[0] == '*') {
        if (!value.empty()) {
            throw std::runtime_error(
                "Style error. Suffix key can only be used with empty value.\n");
        }
        m_matcher.emplace_back(key.substr(1), flags, matcher_t::MT_SUFFIX);
        return;
    }

    if (key == "boundary") {
        if (value.empty() || value == "administrative") {
            flags |= SF_BOUNDARY;
        }
    }

    if (value.empty()) {
        m_matcher.emplace_back(key, flags, matcher_t::MT_KEY);
    } else {
        m_matcher.emplace_back(key + '\0' + value, flags, matcher_t::MT_FULL);
    }
}

gazetteer_style_t::flag_t gazetteer_style_t::find_flag(char const *k,
                                                       char const *v) const
{
    auto klen = std::strlen(k);
    auto vlen = std::strlen(v);

    // full match
    auto fulllen = klen + vlen + 1U;
    for (auto const &e : m_matcher) {
        switch (e.type) {
        case matcher_t::MT_FULL:
            if (e.name.size() == fulllen && strcmp(k, e.name.c_str()) == 0 &&
                memcmp(v, e.name.data() + klen + 1, vlen) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_KEY:
            if (e.name.size() == klen && memcmp(k, e.name.data(), klen) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_PREFIX:
            if (e.name.size() < klen &&
                memcmp(k, e.name.data(), e.name.size()) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_SUFFIX:
            if (e.name.size() < klen &&
                memcmp(k + klen - e.name.size(), e.name.data(),
                       e.name.size()) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_VALUE:
            if (e.name.size() == vlen && memcmp(v, e.name.data(), vlen) == 0) {
                return e.flag;
            }
            break;
        }
    }

    return m_default;
}

void gazetteer_style_t::process_tags(osmium::OSMObject const &o)
{
    clear();

    char const *postcode = nullptr;
    char const *country = nullptr;
    char const *place = nullptr;
    flag_t place_flag;
    bool address_point = false;
    bool interpolation = false;
    bool admin_boundary = false;

    for (auto const &item : o.tags()) {
        char const *k = item.key();
        char const *v = item.value();

        if (strcmp(k, "admin_level") == 0) {
            m_admin_level = atoi(v);
            if (m_admin_level <= 0 || m_admin_level > MAX_ADMINLEVEL)
                m_admin_level = MAX_ADMINLEVEL;
            continue;
        }

        if (m_any_operator_matches && strcmp(k, "operator") == 0) {
            m_operator = v;
        }

        flag_t flag = find_flag(k, v);

        if (flag == 0) {
            continue;
        }

        if (flag & SF_MAIN) {
            if (strcmp(k, "place") == 0) {
                place = v;
                place_flag = flag;
            } else {
                m_main.emplace_back(k, v, flag);
                if ((flag & SF_BOUNDARY) && strcmp(v, "administrative") == 0) {
                    admin_boundary = true;
                }
            }
        }

        if (flag & (SF_NAME | SF_REF)) {
            m_names.emplace_back(k, v);
            if (flag & SF_NAME) {
                m_is_named = true;
            }
        }

        if (flag & SF_ADDRESS) {
            char const *addr_key;
            if (strncmp(k, "addr:", 5) == 0) {
                addr_key = k + 5;
            } else if (strncmp(k, "is_in:", 6) == 0) {
                addr_key = k + 6;
            } else {
                addr_key = k;
            }

            if (strcmp(addr_key, "postcode") == 0) {
                if (!postcode) {
                    postcode = v;
                }
            } else if (strcmp(addr_key, "country") == 0) {
                if (!country && strlen(v) == 2) {
                    country = v;
                }
            } else {
                bool first = std::none_of(
                    m_address.begin(), m_address.end(), [&](ptag_t const &t) {
                        return strcmp(t.first, addr_key) == 0;
                    });
                if (first) {
                    m_address.emplace_back(addr_key, v);
                }
            }
        }

        if (flag & SF_ADDRESS_POINT) {
            address_point = true;
            m_is_named = true;
        }

        if ((flag & SF_POSTCODE) && !postcode) {
            postcode = v;
        }

        if ((flag & SF_COUNTRY) && !country && std::strlen(v) == 2) {
            country = v;
        }

        if (flag & SF_EXTRA) {
            m_extra.emplace_back(k, v);
        }

        if (flag & SF_INTERPOLATION) {
            m_main.emplace_back("place", "houses", SF_MAIN);
            interpolation = true;
        }
    }

    if (postcode) {
        m_address.emplace_back("postcode", postcode);
    }
    if (country) {
        m_address.emplace_back("country", country);
    }
    if (place) {
        if (interpolation || (admin_boundary &&
                              strncmp(place, "isl", 3) != 0)) // island or islet
            m_extra.emplace_back("place", place);
        else
            m_main.emplace_back("place", place, place_flag);
    }
    if (address_point) {
        m_main.emplace_back("place", "house", SF_MAIN | SF_MAIN_FALLBACK);
    } else if (postcode) {
        m_main.emplace_back("place", "postcode", SF_MAIN | SF_MAIN_FALLBACK);
    }
}

void gazetteer_style_t::copy_out(osmium::OSMObject const &o,
                                 std::string const &geom, std::string &buffer)
{
    bool any = false;
    for (auto const &main : m_main) {
        if (!(std::get<2>(main) & SF_MAIN_FALLBACK)) {
            any |= copy_out_maintag(main, o, geom, buffer);
        }
    }

    if (!any) {
        for (auto const &main : m_main) {
            if ((std::get<2>(main) & SF_MAIN_FALLBACK) &&
                copy_out_maintag(main, o, geom, buffer)) {
                break;
            }
        }
    }
}

bool gazetteer_style_t::copy_out_maintag(pmaintag_t const &tag,
                                         osmium::OSMObject const &o,
                                         std::string const &geom,
                                         std::string &buffer)
{
    std::string name;
    if (std::get<2>(tag) & SF_MAIN_NAMED_KEY) {
        name = domain_name(std::get<0>(tag), o.tags());
        if (name.empty())
            return false;
    }

    if (std::get<2>(tag) & SF_MAIN_NAMED) {
        if (name.empty() && !m_is_named) {
            return false;
        }
    }

    // osm_type
    buffer += (char)toupper(osmium::item_type_to_char(o.type()));
    buffer += '\t';
    // osm_id
    buffer += (m_single_fmt % o.id()).str();
    // class
    escape(std::get<0>(tag), buffer);
    buffer += '\t';
    // type
    escape(std::get<1>(tag), buffer);
    buffer += '\t';
    // names
    if (!name.empty()) {
        buffer += name;
        buffer += '\t';
    } else {
        bool first = true;
        // operator will be ignored on anything but these classes
        if (m_operator && (std::get<2>(tag) & SF_MAIN_OPERATOR)) {
            buffer += "\"operator\"=>\"";
            escape_array_record(m_operator, buffer);
            buffer += "\"";
            first = false;
        }
        for (auto const &entry : m_names) {
            if (first) {
                first = false;
            } else {
                buffer += ',';
            }

            buffer += "\"";
            escape_array_record(entry.first, buffer);
            buffer += "\"=>\"";
            escape_array_record(entry.second, buffer);
            buffer += "\"";
        }

        buffer += first ? "\\N\t" : "\t";
    }
    // admin_level
    buffer += (m_single_fmt % m_admin_level).str();
    // address
    if (m_address.empty()) {
        buffer += "\\N\t";
    } else {
        for (auto const &a : m_address) {
            buffer += "\"";
            escape_array_record(a.first, buffer);
            buffer += "\"=>\"";
            if (strcmp(a.first, "tiger:county") == 0) {
                auto *end = strchr(a.second, ',');
                if (end) {
                    auto len = (std::string::size_type)(end - a.second);
                    escape_array_record(std::string(a.second, len).c_str(),
                                        buffer);
                } else {
                    escape_array_record(a.second, buffer);
                }
                buffer += " county";
            } else {
                escape_array_record(a.second, buffer);
            }
            buffer += "\",";
        }
        buffer[buffer.length() - 1] = '\t';
    }
    // extra tags
    if (m_extra.empty()) {
        buffer += "\\N\t";
    } else {
        for (auto const &entry : m_extra) {
            buffer += "\"";
            escape_array_record(entry.first, buffer);
            buffer += "\"=>\"";
            escape_array_record(entry.second, buffer);
            buffer += "\",";
        }
        buffer[buffer.length() - 1] = '\t';
    }
    // add the geometry - encoding it to hex along the way
    ewkb::writer_t::write_as_hex(buffer, geom);
    buffer += '\n';

    return true;
}
