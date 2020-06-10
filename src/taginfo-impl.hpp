#ifndef OSM2PGSQL_TAGINFO_IMPL_HPP
#define OSM2PGSQL_TAGINFO_IMPL_HPP

#include "osmtypes.hpp"
#include "taginfo.hpp"

#include <osmium/index/nwr_array.hpp>

#include <string>
#include <utility>
#include <vector>

enum column_flags : unsigned int
{
    FLAG_POLYGON = 1, /* For polygon table */
    FLAG_LINEAR = 2,  /* For lines table */
    FLAG_NOCACHE = 4, /* Optimisation: don't bother remembering this one */
    FLAG_DELETE = 8,  /* These tags should be simply deleted on sight */
    FLAG_NOCOLUMN =
        16, /* objects without column but should be listed in database hstore column */
    FLAG_PHSTORE =
        17, /* same as FLAG_NOCOLUMN & FLAG_POLYGON to maintain compatibility */
    FLAG_INT_TYPE = 32, /* column value should be converted to integer */
    FLAG_REAL_TYPE = 64 /* column value should be converted to double */
};

/* Table columns, representing key= tags */
struct taginfo
{
    ColumnType column_type() const
    {
        if (flags & FLAG_INT_TYPE) {
            return ColumnType::INT;
        }
        if (flags & FLAG_REAL_TYPE) {
            return ColumnType::REAL;
        }
        return ColumnType::TEXT;
    }

    std::string name;
    std::string type;
    unsigned int flags = 0;
};

/* list of exported tags */
class export_list
{
public:
    void add(osmium::item_type type, taginfo const &info);
    std::vector<taginfo> const &get(osmium::item_type type) const noexcept;

    columns_t normal_columns(osmium::item_type type) const;
    bool has_column(osmium::item_type type, char const *name) const;

private:
    osmium::nwr_array<std::vector<taginfo>> m_export_list;
};

/* Parse a comma or whitespace delimited list of tags to apply to
 * a style file entry, returning the OR-ed set of flags.
 */
unsigned parse_tag_flags(std::string const &flags, int lineno);

/* Parse an osm2pgsql "pgsql" backend format style file, putting
 * the results in the `exlist` argument.
 *
 * Returns `true` if the 'way_area' column should (implicitly) exist, or
 * `false` if it should be suppressed.
 */
bool read_style_file(std::string const &filename, export_list *exlist);

#endif // OSM2PGSQL_TAGINFO_IMPL_HPP
