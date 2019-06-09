#ifndef GAZETTEER_STYLE_HPP
#define GAZETTEER_STYLE_HPP

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/format.hpp>
#include <osmium/osm/metadata_options.hpp>

#include "db-copy.hpp"

class gazetteer_style_t
{
    using flag_t = uint16_t;
    using ptag_t = std::pair<char const *, char const *>;
    using pmaintag_t = std::tuple<char const *, char const *, flag_t>;

    enum style_flags
    {
        SF_MAIN = 1 << 0,
        SF_MAIN_NAMED = 1 << 1,
        SF_MAIN_NAMED_KEY = 1 << 2,
        SF_MAIN_FALLBACK = 1 << 3,
        SF_MAIN_OPERATOR = 1 << 4,
        SF_NAME = 1 << 5,
        SF_REF = 1 << 6,
        SF_ADDRESS = 1 << 7,
        SF_ADDRESS_POINT = 1 << 8,
        SF_POSTCODE = 1 << 9,
        SF_COUNTRY = 1 << 10,
        SF_EXTRA = 1 << 11,
        SF_INTERPOLATION = 1 << 12,
        SF_BOUNDARY = 1 << 13, // internal flag for boundaries
    };

    enum class matcher_t
    {
        MT_FULL,
        MT_KEY,
        MT_PREFIX,
        MT_SUFFIX,
        MT_VALUE
    };

    struct string_with_flag_t
    {
        std::string name;
        flag_t flag;
        matcher_t type;

        string_with_flag_t(std::string const &n, flag_t f, matcher_t t)
        : name(n), flag(f), type(t)
        {
        }
    };

    using flag_list_t = std::vector<string_with_flag_t>;

public:
    void load_style(std::string const &filename);
    void process_tags(osmium::OSMObject const &o);
    bool copy_out(osmium::OSMObject const &o, std::string const &geom,
                  db_copy_mgr_t &buffer);
    bool has_place(std::string const &cls) const;

    bool has_data() const { return !m_main.empty(); }

private:
    bool add_metadata_style_entry(std::string const &key);
    void add_style_entry(std::string const &key, std::string const &value,
                         flag_t flags);
    flag_t parse_flags(std::string const &str);
    flag_t find_flag(char const *k, char const *v) const;

    bool copy_out_maintag(pmaintag_t const &tag, osmium::OSMObject const &o,
                          std::string const &geom, db_copy_mgr_t &buffer);
    void clear();

    // Style data.
    flag_list_t m_matcher;
    flag_t m_default{0};
    bool m_any_operator_matches{false};

    // Cached OSM object data.

    /// class/type pairs to include
    std::vector<pmaintag_t> m_main;
    /// name tags to include
    std::vector<ptag_t> m_names;
    /// extratags to include
    std::vector<ptag_t> m_extra;
    /// addresstags to include
    std::vector<ptag_t> m_address;
    /// value of operator tag
    char const *m_operator;
    /// admin level
    int m_admin_level;
    /// True if there is an actual name to the object (not a ref).
    bool m_is_named;

    /// which metadata fields of the OSM objects should be written to the output
    osmium::metadata_options m_metadata_fields{"none"};
};

#endif
