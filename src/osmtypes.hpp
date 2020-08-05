#ifndef OSM2PGSQL_OSMTYPES_HPP
#define OSM2PGSQL_OSMTYPES_HPP

/**
 * \file
 *
 * This file is part of osm2pgsql (https://github.com/openstreetmap/osm2pgsql).
 *
 * In this file some basic (OSM) data types are defined.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <osmium/builder/attr.hpp>
#include <osmium/geom/coordinates.hpp>
#include <osmium/osm.hpp>

using osmid_t = std::int64_t;

struct member
{
    osmium::item_type type;
    osmid_t id;
    std::string role;

    operator osmium::builder::attr::member_type const() const
    {
        return osmium::builder::attr::member_type(type, id, role.c_str());
    }

    member(osmium::item_type t, osmid_t i, std::string const &r)
    : type(t), id(i), role(r)
    {}
};

struct memberlist_t : public std::vector<member>
{
    memberlist_t() {}

    explicit memberlist_t(osmium::RelationMemberList const &list)
    {
        for (auto const &m : list) {
            emplace_back(m.type(), m.ref(), m.role());
        }
    }

    std::vector<osmium::builder::attr::member_type> for_builder() const
    {
        std::vector<osmium::builder::attr::member_type> ret;
        for (auto const &m : *this) {
            ret.emplace_back(m.type, m.id, m.role.c_str());
        }

        return ret;
    }
};

struct tag_t
{
    std::string key;
    std::string value;

    operator std::pair<char const *, char const *> const() const noexcept
    {
        return std::pair<char const *, char const *>(key.c_str(),
                                                     value.c_str());
    }

    tag_t(std::string const &k, std::string const &v) : key(k), value(v) {}
};

/**
 * An editable list of tags.
 *
 * The list is not sorted.
 */
class taglist_t
{
public:
    using iterator = std::vector<tag_t>::iterator;
    using const_iterator = std::vector<tag_t>::const_iterator;

    taglist_t() = default;

    explicit taglist_t(osmium::TagList const &list)
    {
        for (auto const &t : list) {
            m_tags.emplace_back(t.key(), t.value());
        }
    }

    /// Add attributes from OSM object as pseudo-tags to list
    void add_attributes(osmium::OSMObject const &obj)
    {
        m_tags.emplace_back("osm_user", obj.user());
        m_tags.emplace_back("osm_uid", std::to_string(obj.uid()));
        m_tags.emplace_back("osm_version", std::to_string(obj.version()));
        m_tags.emplace_back("osm_timestamp", obj.timestamp().to_iso());
        m_tags.emplace_back("osm_changeset", std::to_string(obj.changeset()));
    }

    /// Is the tag list empty?
    bool empty() const noexcept { return m_tags.empty(); }

    /// Return size of the tag list
    std::size_t size() const noexcept { return m_tags.size(); }

    const_iterator cbegin() const noexcept { return m_tags.cbegin(); }

    const_iterator cend() const noexcept { return m_tags.cend(); }

    const_iterator begin() const noexcept { return m_tags.cbegin(); }

    const_iterator end() const noexcept { return m_tags.cend(); }

    tag_t const &operator[](std::size_t idx) const noexcept
    {
        return m_tags[idx];
    }

    /// Is there a tag with this key in the list?
    bool contains(std::string const &key) const
    {
        return find_by_key(key) != m_tags.cend();
    }

    /**
     * Find index of tag with key in list. Return max value of size_t if this
     * key was not found.
     */
    std::size_t indexof(std::string const &key) const noexcept
    {
        for (std::size_t i = 0; i < m_tags.size(); ++i) {
            if (m_tags[i].key == key) {
                return i;
            }
        }

        return std::numeric_limits<std::size_t>::max();
    }

    std::string const *get(std::string const &key) const
    {
        auto const it = find_by_key(key);
        if (it == m_tags.cend()) {
            return nullptr;
        }
        return &(it->value);
    }

    static bool value_to_bool(char const *value, bool defval) noexcept
    {
        if (!defval &&
            (std::strcmp(value, "yes") == 0 ||
             std::strcmp(value, "true") == 0 || std::strcmp(value, "1") == 0)) {
            return true;
        }

        if (defval && (std::strcmp(value, "no") == 0 ||
                       std::strcmp(value, "false") == 0 ||
                       std::strcmp(value, "0") == 0)) {
            return false;
        }

        return defval;
    }

    bool get_bool(std::string const &key, bool defval) const
    {
        auto const it = find_by_key(key);
        if (it == m_tags.cend()) {
            return defval;
        }

        return value_to_bool(it->value.c_str(), defval);
    }

    /// Add tag to list without checking for duplicates
    void add_tag(char const *key, char const *value)
    {
        m_tags.emplace_back(key, value);
    }

    /// Add tag to list if there is no tag with that key yet
    template <typename V>
    void add_tag_if_not_exists(char const *key, V &&value)
    {
        if (!contains(key)) {
            m_tags.emplace_back(key, std::forward<V>(value));
        }
    }

    /// Add tag to list if there is no tag with that key yet
    void add_tag_if_not_exists(tag_t const &t)
    {
        if (!contains(t.key)) {
            m_tags.push_back(t);
        }
    }

    /// Insert or update tag in list
    template <typename V>
    void set(char const *key, V &&value)
    {
        auto const it = find_by_key(key);
        if (it == m_tags.end()) {
            m_tags.emplace_back(key, std::forward<V>(value));
        } else {
            it->value = std::forward<V>(value);
        }
    }

private:
    iterator find_by_key(std::string const &key)
    {
        return std::find_if(m_tags.begin(), m_tags.end(),
                            [&key](tag_t const &t) { return t.key == key; });
    }

    const_iterator find_by_key(std::string const &key) const
    {
        return std::find_if(m_tags.cbegin(), m_tags.cend(),
                            [&key](tag_t const &t) { return t.key == key; });
    }

    std::vector<tag_t> m_tags;
}; // class taglist_t

struct idlist_t : public std::vector<osmid_t>
{
    // Get all constructors from std::vector
    using vector<osmid_t>::vector;

    // Even though we got all constructors from std::vector we need this on
    // some compilers/libraries for some reason.
    idlist_t() = default;

    explicit idlist_t(osmium::NodeRefList const &list)
    {
        for (auto const &n : list) {
            push_back(n.ref());
        }
    }
};

using rolelist_t = std::vector<char const *>;

#endif // OSM2PGSQL_OSMTYPES_HPP
