#ifndef OSM2PGSQL_DOMAIN_MATCHER_HPP
#define OSM2PGSQL_DOMAIN_MATCHER_HPP

#include <osmium/osm/tag.hpp>

#include <cstring>

/**
 * Returns the tag specific name, if applicable.
 *
 * OSM tags may contain name tags that refer to one of the other tags
 * in the tag set. For example, the name of a bridge is tagged as
 * bridge:name=Foo to not confuse it with the name of the highway
 * going over the bridge. This matcher checks if a tag is such a name tag
 * for the given tag key and returns the name key without the prefix
 * if it matches.
 */
class DomainMatcher
{
public:
    explicit DomainMatcher(char const *cls) noexcept
    : m_domain(cls), m_len(std::strlen(cls))
    {}

    char const *operator()(osmium::Tag const &t) const noexcept
    {
        if (std::strncmp(t.key(), m_domain, m_len) == 0 &&
            std::strncmp(t.key() + m_len, ":name", 5) == 0 &&
            (t.key()[m_len + 5] == '\0' || t.key()[m_len + 5] == ':')) {
            return t.key() + m_len + 1;
        }

        return nullptr;
    }

private:
    char const *m_domain;
    size_t m_len;
};

#endif // OSM2PGSQL_DOMAIN_MATCHER_HPP
