#ifndef TAGINFO_IMPL_HPP
#define TAGINFO_IMPL_HPP

#include "taginfo.hpp"
#include "osmtypes.hpp"
#include <string>
#include <vector>

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

    int num_tables;
    std::vector<std::vector<taginfo> > exportList; /* Indexed by enum OsmType */
};

#endif /* TAGINFO_IMPL_HPP */
