#ifndef TAGINFO_IMPL_HPP
#define TAGINFO_IMPL_HPP

#include "taginfo.hpp"
#include "osmtypes.hpp"
#include <string>
#include <vector>
#include <utility>

enum column_flags {
  FLAG_POLYGON = 1,   /* For polygon table */
  FLAG_LINEAR = 2,    /* For lines table */
  FLAG_NOCACHE = 4,   /* Optimisation: don't bother remembering this one */
  FLAG_DELETE = 8,    /* These tags should be simply deleted on sight */
  FLAG_NOCOLUMN = 16, /* objects without column but should be listed in database hstore column */
  FLAG_PHSTORE = 17,  /* same as FLAG_NOCOLUMN & FLAG_POLYGON to maintain compatibility */
  FLAG_INT_TYPE = 32, /* column value should be converted to integer */
  FLAG_REAL_TYPE = 64 /* column value should be converted to double */
};

struct taginfo {
    taginfo();
    taginfo(const taginfo &);

    ColumnType column_type() const {
        if (flags & FLAG_INT_TYPE) {
            return COLUMN_TYPE_INT;
        }
        if (flags & FLAG_REAL_TYPE) {
            return COLUMN_TYPE_REAL;
        }
        return COLUMN_TYPE_TEXT;
    }

    std::string name, type;
    unsigned flags;
};

struct export_list {
    void add(osmium::item_type id, const taginfo &info);
    std::vector<taginfo> &get(osmium::item_type id);
    const std::vector<taginfo> &get(osmium::item_type id) const;

    columns_t normal_columns(osmium::item_type id) const;
    bool has_column(osmium::item_type id, char const *name) const;

    std::vector<std::vector<taginfo> > exportList; /* Indexed osmium nwr index */
};

/* Parse a comma or whitespace delimited list of tags to apply to
 * a style file entry, returning the OR-ed set of flags.
 */
int parse_tag_flags(const char *flags, int lineno);

/* Parse an osm2pgsql "pgsql" backend format style file, putting
 * the results in the `exlist` argument.
 *
 * Returns 1 if the 'way_area' column should (implicitly) exist, or
 * 0 if it should be suppressed.
 */
int read_style_file( const std::string &filename, export_list *exlist );

#endif /* TAGINFO_IMPL_HPP */
