#ifndef TAGINFO_IMPL_HPP
#define TAGINFO_IMPL_HPP

#include "taginfo.hpp"
#include "osmtypes.hpp"
#include <string>
#include <vector>
#include <utility>

#define FLAG_POLYGON 1    /* For polygon table */
#define FLAG_LINEAR  2    /* For lines table */
#define FLAG_NOCACHE 4    /* Optimisation: don't bother remembering this one */
#define FLAG_DELETE  8    /* These tags should be simply deleted on sight */
#define FLAG_PHSTORE 17   /* polygons without own column but listed in hstore this implies FLAG_POLYGON */

struct taginfo {
    taginfo();
    taginfo(const taginfo &);

    std::string name, type;
    int flags;
};

struct export_list {
    export_list();

    void add(enum OsmType id, const taginfo &info);
    std::vector<taginfo> &get(enum OsmType id);
    const std::vector<taginfo> &get(enum OsmType id) const;

    std::vector<std::pair<std::string, std::string> > normal_columns(enum OsmType id) const;

    int num_tables;
    std::vector<std::vector<taginfo> > exportList; /* Indexed by enum OsmType */
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
