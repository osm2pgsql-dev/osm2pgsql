#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <boost/property_tree/json_parser.hpp>
#include <osmium/osm.hpp>

#include "domain-matcher.hpp"
#include "format.hpp"
#include "gazetteer-style.hpp"
#include "logging.hpp"
#include "pgsql.hpp"
#include "wkb.hpp"

namespace {

enum : int
{
    MAX_ADMINLEVEL = 15
};

} // anonymous namespace

namespace pt = boost::property_tree;

void db_deleter_place_t::delete_rows(std::string const &table,
                                     std::string const &, pg_conn_t *conn)
{
    assert(!m_deletables.empty());

    fmt::memory_buffer sql;
    // Need a VALUES line for each deletable: type (3 bytes), id (15 bytes),
    // class list (20 bytes), braces etc. (5 bytes). And additional space for
    // the remainder of the SQL command.
    sql.reserve(m_deletables.size() * 43 + 200);

    fmt::format_to(sql, "DELETE FROM {} p USING (VALUES ", table);

    for (auto const &item : m_deletables) {
        fmt::format_to(sql, "('{}',{},", item.osm_type, item.osm_id);
        if (item.classes.empty()) {
            fmt::format_to(sql, "ARRAY[]::text[]),");
        } else {
            fmt::format_to(sql, "ARRAY[{}]),", item.classes);
        }
    }

    // remove the final comma
    sql.resize(sql.size() - 1);

    fmt::format_to(sql, ") AS t (osm_type, osm_id, classes) WHERE"
                        " p.osm_type = t.osm_type AND p.osm_id = t.osm_id"
                        " AND NOT p.class = ANY(t.classes)");

    conn->exec(fmt::to_string(sql));
}

void gazetteer_style_t::clear()
{
    m_main.clear();
    m_names.clear();
    m_extra.clear();
    m_address.clear();
    m_operator = nullptr;
    m_admin_level = MAX_ADMINLEVEL;
}

std::string gazetteer_style_t::class_list() const
{
    fmt::memory_buffer buf;

    for (auto const &m : m_main) {
        fmt::format_to(buf, FMT_STRING("'{}',"), std::get<0>(m));
    }

    if (buf.size() > 0) {
        buf.resize(buf.size() - 1);
    }

    return fmt::to_string(buf);
}

void gazetteer_style_t::load_style(std::string const &filename)
{
    log_info("Parsing gazetteer style file '{}'.", filename);
    pt::ptree root;

    pt::read_json(filename, root);

    for (auto const &entry : root) {
        for (auto const &tag : entry.second.get_child("keys")) {
            for (auto const &value : entry.second.get_child("values")) {
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
        auto const end = str.find(',', start);

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
        }

        if (item == "main") {
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
            throw std::runtime_error{"Unknown flag in style file."};
        }
    }

    return out;
}

bool gazetteer_style_t::add_metadata_style_entry(std::string const &key)
{
    if (key == "osm_version") {
        m_metadata_fields.set_version(true);
    } else if (key == "osm_timestamp") {
        m_metadata_fields.set_timestamp(true);
    } else if (key == "osm_changeset") {
        m_metadata_fields.set_changeset(true);
    } else if (key == "osm_uid") {
        m_metadata_fields.set_uid(true);
    } else if (key == "osm_user") {
        m_metadata_fields.set_user(true);
    } else {
        return false;
    }
    return true;
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
            throw std::runtime_error{"Style error. Ambiguous key '*'."};
        }
        if (!value.empty()) {
            throw std::runtime_error{
                "Style error. Prefix key can only be used with empty value."};
        }
        m_matcher.emplace_back(key.substr(0, key.size() - 1), flags,
                               matcher_t::MT_PREFIX);
        return;
    }

    // suffix: dito
    if (key[0] == '*') {
        if (!value.empty()) {
            throw std::runtime_error{
                "Style error. Suffix key can only be used with empty value."};
        }
        m_matcher.emplace_back(key.substr(1), flags, matcher_t::MT_SUFFIX);
        return;
    }

    if (key == "boundary") {
        if (value.empty() || value == "administrative") {
            flags |= SF_BOUNDARY;
        }
    }

    if (add_metadata_style_entry(key)) {
        if (!value.empty()) {
            throw std::runtime_error{"Style error. Rules for OSM metadata "
                                     "attributes must have an empty value."};
        }
        if (flags != SF_EXTRA) {
            throw std::runtime_error{"Style error. Rules for OSM metadata "
                                     "attributes must have the style flag "
                                     "\"extra\" and no other flag."};
        }
        return;
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
    auto const klen = std::strlen(k);
    auto const vlen = std::strlen(v);

    // full match
    auto const fulllen = klen + vlen + 1U;
    for (auto const &e : m_matcher) {
        switch (e.type) {
        case matcher_t::MT_FULL:
            if (e.name.size() == fulllen &&
                std::strcmp(k, e.name.c_str()) == 0 &&
                std::memcmp(v, e.name.data() + klen + 1, vlen) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_KEY:
            if (e.name.size() == klen &&
                std::memcmp(k, e.name.data(), klen) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_PREFIX:
            if (e.name.size() < klen &&
                std::memcmp(k, e.name.data(), e.name.size()) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_SUFFIX:
            if (e.name.size() < klen &&
                std::memcmp(k + klen - e.name.size(), e.name.data(),
                            e.name.size()) == 0) {
                return e.flag;
            }
            break;
        case matcher_t::MT_VALUE:
            if (e.name.size() == vlen &&
                std::memcmp(v, e.name.data(), vlen) == 0) {
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

    bool has_postcode = false;
    bool has_country = false;
    char const *place = nullptr;
    flag_t place_flag;
    bool address_point = false;
    bool interpolation = false;
    bool admin_boundary = false;
    bool postcode_fallback = false;
    bool is_named = false;

    for (auto const &item : o.tags()) {
        char const *const k = item.key();
        char const *const v = item.value();

        if (std::strcmp(k, "admin_level") == 0) {
            m_admin_level = std::atoi(v);
            if (m_admin_level <= 0 || m_admin_level > MAX_ADMINLEVEL) {
                m_admin_level = MAX_ADMINLEVEL;
            }
            continue;
        }

        if (m_any_operator_matches && std::strcmp(k, "operator") == 0) {
            m_operator = v;
        }

        flag_t const flag = find_flag(k, v);

        if (flag == 0) {
            continue;
        }

        if (flag & SF_MAIN) {
            if (std::strcmp(k, "place") == 0) {
                place = v;
                place_flag = flag;
            } else {
                m_main.emplace_back(k, v, flag);
                if ((flag & SF_BOUNDARY) &&
                    std::strcmp(v, "administrative") == 0) {
                    admin_boundary = true;
                }
            }
        }

        if (flag & (SF_NAME | SF_REF)) {
            m_names.emplace_back(k, v);
            if (flag & SF_NAME) {
                is_named = true;
            }
        }

        if (flag & SF_ADDRESS) {
            char const *addr_key;
            if (std::strncmp(k, "addr:", 5) == 0) {
                addr_key = k + 5;
            } else if (std::strncmp(k, "is_in:", 6) == 0) {
                addr_key = k + 6;
            } else {
                addr_key = k;
            }

            // country and postcode are handled specially, ignore them here
            if (std::strcmp(addr_key, "country") != 0 &&
                std::strcmp(addr_key, "postcode") != 0) {
                bool const first = std::none_of(
                    m_address.begin(), m_address.end(), [&](ptag_t const &t) {
                        return std::strcmp(t.first, addr_key) == 0;
                    });
                if (first) {
                    m_address.emplace_back(addr_key, v);
                }
            }
        }

        if (flag & SF_ADDRESS_POINT) {
            address_point = true;
            is_named = true;
        }

        if ((flag & SF_POSTCODE) && !has_postcode) {
            has_postcode = true;
            m_address.emplace_back("postcode", v);
            if (flag & SF_MAIN_FALLBACK) {
                postcode_fallback = true;
            }
        }

        if ((flag & SF_COUNTRY) && !has_country && std::strlen(v) == 2) {
            has_country = true;
            m_address.emplace_back("country", v);
        }

        if (flag & SF_EXTRA) {
            m_extra.emplace_back(k, v);
        }

        if (flag & SF_INTERPOLATION) {
            m_main.emplace_back("place", "houses", SF_MAIN);
            interpolation = true;
        }
    }

    if (place) {
        if (interpolation || (admin_boundary && std::strncmp(place, "isl", 3) !=
                                                    0)) { // island or islet
            m_extra.emplace_back("place", place);
        } else {
            m_main.emplace_back("place", place, place_flag);
        }
    }

    filter_main_tags(is_named, o.tags());

    if (m_main.empty()) {
        if (address_point) {
            m_main.emplace_back("place", "house", SF_MAIN | SF_MAIN_FALLBACK);
        } else if (postcode_fallback && has_postcode) {
            m_main.emplace_back("place", "postcode",
                                SF_MAIN | SF_MAIN_FALLBACK);
        }
    }
}

void gazetteer_style_t::filter_main_tags(bool is_named,
                                         osmium::TagList const &tags)
{
    // first throw away unnamed mains
    auto mend =
        std::remove_if(m_main.begin(), m_main.end(), [&](pmaintag_t const &t) {
            auto const flags = std::get<2>(t);

            if (flags & SF_MAIN_NAMED) {
                return !is_named;
            }

            if (flags & SF_MAIN_NAMED_KEY) {
                return !std::any_of(tags.begin(), tags.end(),
                                    DomainMatcher{std::get<0>(t)});
            }

            return false;
        });

    // any non-fallback mains left?
    bool const has_primary =
        std::any_of(m_main.begin(), mend, [](pmaintag_t const &t) {
            return !(std::get<2>(t) & SF_MAIN_FALLBACK);
        });

    if (has_primary) {
        // remove all fallbacks
        mend = std::remove_if(m_main.begin(), mend, [&](pmaintag_t const &t) {
            return (std::get<2>(t) & SF_MAIN_FALLBACK);
        });
        m_main.erase(mend, m_main.end());
    } else if (mend == m_main.begin()) {
        m_main.clear();
    } else {
        // remove everything except the first entry
        m_main.resize(1);
    }
}

void gazetteer_style_t::copy_out(osmium::OSMObject const &o,
                                 std::string const &geom,
                                 copy_mgr_t &buffer) const
{
    for (auto const &tag : m_main) {
        buffer.prepare();
        // osm_id
        buffer.add_column(o.id());
        // osm_type
        char const osm_type[2] = {
            (char)toupper(osmium::item_type_to_char(o.type())), '\0'};
        buffer.add_column(osm_type);
        // class
        buffer.add_column(std::get<0>(tag));
        // type
        buffer.add_column(std::get<1>(tag));
        // names
        if (std::get<2>(tag) & SF_MAIN_NAMED_KEY) {
            DomainMatcher m{std::get<0>(tag)};
            buffer.new_hash();
            for (auto const &t : o.tags()) {
                char const *const k = m(t);
                if (k) {
                    buffer.add_hash_elem(k, t.value());
                }
            }
            buffer.finish_hash();
        } else {
            bool first = true;
            // operator will be ignored on anything but these classes
            if (m_operator && (std::get<2>(tag) & SF_MAIN_OPERATOR)) {
                buffer.new_hash();
                buffer.add_hash_elem("operator", m_operator);
                first = false;
            }
            for (auto const &entry : m_names) {
                if (first) {
                    buffer.new_hash();
                    first = false;
                }

                buffer.add_hash_elem(entry.first, entry.second);
            }

            if (first) {
                buffer.add_null_column();
            } else {
                buffer.finish_hash();
            }
        }
        // admin_level
        buffer.add_column(m_admin_level);
        // address
        if (m_address.empty()) {
            buffer.add_null_column();
        } else {
            buffer.new_hash();
            for (auto const &a : m_address) {
                if (std::strcmp(a.first, "tiger:county") == 0) {
                    std::string term;
                    auto const *const end = std::strchr(a.second, ',');
                    if (end) {
                        auto const len =
                            (std::string::size_type)(end - a.second);
                        term = std::string(a.second, len);
                    } else {
                        term = a.second;
                    }
                    term += " county";
                    buffer.add_hash_elem(a.first, term);
                } else {
                    buffer.add_hash_elem(a.first, a.second);
                }
            }
            buffer.finish_hash();
        }
        // extra tags
        if (m_extra.empty() && m_metadata_fields.none()) {
            buffer.add_null_column();
        } else {
            buffer.new_hash();
            for (auto const &entry : m_extra) {
                buffer.add_hash_elem(entry.first, entry.second);
            }
            if (m_metadata_fields.version() && o.version()) {
                buffer.add_hstore_num_noescape<osmium::object_version_type>(
                    "osm_version", o.version());
            }
            if (m_metadata_fields.uid() && o.uid()) {
                buffer.add_hstore_num_noescape<osmium::user_id_type>("osm_uid",
                                                                     o.uid());
            }
            if (m_metadata_fields.user() && o.user() && *(o.user()) != '\0') {
                buffer.add_hash_elem("osm_user", o.user());
            }
            if (m_metadata_fields.changeset() && o.changeset()) {
                buffer.add_hstore_num_noescape<osmium::changeset_id_type>(
                    "osm_changeset", o.changeset());
            }
            if (m_metadata_fields.timestamp() && o.timestamp()) {
                std::string timestamp = o.timestamp().to_iso();
                buffer.add_hash_elem_noescape("osm_timestamp",
                                              timestamp.c_str());
            }
            buffer.finish_hash();
        }
        // add the geometry - encoding it to hex along the way
        buffer.add_hex_geom(geom);

        buffer.finish_line();
    }
}
