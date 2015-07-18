#ifndef PARSE_H
#define PARSE_H

#include "osmtypes.hpp"

#include <time.h>
#include <string>
#include <memory>
#include <boost/optional.hpp>

typedef enum { FILETYPE_NONE, FILETYPE_OSM, FILETYPE_OSMCHANGE, FILETYPE_PLANETDIFF } filetypes_t;
typedef enum { ACTION_NONE, ACTION_CREATE, ACTION_MODIFY, ACTION_DELETE } actions_t;

class parse_t;
class osmdata_t;
struct reprojection;

class bbox_t
{
public:
    bbox_t(const boost::optional<std::string> &bbox);
    bool inside(double lat, double lon) const
    {
        return !m_valid || (lat >= m_minlat && lat <= m_maxlat
                            && lon >= m_minlon && lon <= m_maxlon);
    }

private:
    void parse_bbox(const std::string &bbox);

    bool m_valid;
    double m_minlon, m_minlat, m_maxlon, m_maxlat;
};

struct parse_stats_t
{
public:
    void update(const parse_stats_t &other);
    void print_summary() const;
    void print_status() const;

    inline void add_node(osmid_t id)
    {
        if(id > max_node)
            max_node = id;
        if (count_node == 0)
            time(&start_node);
        count_node++;
        if (count_node % 10000 == 0)
            print_status();
    }

    inline void add_way(osmid_t id)
    {
        if (id > max_way)
            max_way = id;
        if (count_way == 0)
            time(&start_way);
        count_way++;
        if (count_way % 1000 == 0)
            print_status();
    }

    inline void add_rel(osmid_t id)
    {
        if (id > max_rel)
            max_rel = id;
        if (count_rel == 0)
            time(&start_rel);
        count_rel++;
        if(count_rel % 10 == 0)
           print_status();
    }

private:
    osmid_t count_node = 0;
    osmid_t max_node = 0;
    osmid_t count_way = 0;
    osmid_t max_way = 0;
    osmid_t count_rel = 0;
    osmid_t max_rel = 0;

    time_t start_node = 0;
    time_t start_way = 0;
    time_t start_rel = 0;
};


class parse_delegate_t
{
public:
     parse_delegate_t(int extra_attributes, const boost::optional<std::string> &bbox, std::shared_ptr<reprojection> projection, bool append);
     ~parse_delegate_t();

    void stream_file(const std::string &input_reader, const std::string &filename,
                    osmdata_t *osmdata);
    void print_summary() const;

private:
    parse_delegate_t();
    std::unique_ptr<parse_t> get_input_reader(const std::string &input_reader,
                                              const std::string &filename);

    int m_extra_attributes;
    std::shared_ptr<reprojection> m_proj;
    bbox_t m_bbox;
    bool m_append;
    parse_stats_t m_stats;
};

class parse_t
{
public:
    parse_t(int extra_attributes_, const bbox_t &bbox_,
            const reprojection *projection_);

    virtual void stream_file(const std::string &filename, osmdata_t *osmdata) = 0;

    parse_stats_t const &get_stats() const { return stats; }

protected:
    /* Since {node,way} elements are not nested we can guarantee the
    values in an end tag must match those of the corresponding
    start tag and can therefore be cached.
    */
    double node_lon, node_lat;
    taglist_t tags;
    idlist_t nds;
    memberlist_t members;
    osmid_t osm_id;
    filetypes_t filetype;
    actions_t action;
    bool parallel_indexing;

    const int extra_attributes;
    const reprojection *proj;
    const bbox_t &bbox;

    parse_stats_t stats;
};

#endif
