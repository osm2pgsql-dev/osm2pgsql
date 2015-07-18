#include "config.h"
#include "parse.hpp"
#include "parse-o5m.hpp"
#include "parse-osmium.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <algorithm>


#define INIT_MAX_MEMBERS 64
#define INIT_MAX_NODES  4096

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

bbox_t::bbox_t(const boost::optional<std::string> &bbox)
{
    m_valid = bool(bbox);
    if (m_valid)
        parse_bbox(*bbox);
}

void bbox_t::parse_bbox(const std::string &bbox)
{
    int n = sscanf(bbox.c_str(), "%lf,%lf,%lf,%lf",
                   &m_minlon, &m_minlat, &m_maxlon, &m_maxlat);
    if (n != 4)
        throw std::runtime_error("Bounding box must be specified like: minlon,minlat,maxlon,maxlat\n");

    if (m_maxlon <= m_minlon)
        throw std::runtime_error("Bounding box failed due to maxlon <= minlon\n");

    if (m_maxlat <= m_minlat)
        throw std::runtime_error("Bounding box failed due to maxlat <= minlat\n");

    fprintf(stderr, "Applying Bounding box: %f,%f to %f,%f\n", m_minlon, m_minlat, m_maxlon, m_maxlat);
}

void parse_stats_t::update(const parse_stats_t &other)
{
    count_node += other.count_node;
    count_way += other.count_way;
    count_rel += other.count_rel;
    max_node = std::max(other.max_node, max_node);
    max_way = std::max(other.max_way, max_way);
    max_rel = std::max(other.max_rel, max_rel);
    start_node = start_node == 0 ? other.start_node : start_node;
    start_way = start_way == 0 ? other.start_way : start_way;
    start_rel = start_rel == 0 ? other.start_rel : start_rel;
}

void parse_stats_t::print_summary() const
{
    time_t now = time(nullptr);
    time_t end_nodes = start_way > 0 ? start_way : now;
    time_t end_way = start_rel > 0 ? start_rel : now;
    time_t end_rel = now;

    fprintf(stderr,
            "Node stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
            count_node, max_node,
            count_node > 0 ? (int) (end_nodes - start_node) : 0);
    fprintf(stderr,
            "Way stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
            count_way, max_way,
            count_way > 0 ? (int) (end_way - start_way) : 0);
    fprintf(stderr,
            "Relation stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
            count_rel, max_rel,
            count_rel > 0 ? (int) (end_rel - start_rel) : 0);
}

void parse_stats_t::print_status() const
{
    time_t now = time(nullptr);
    time_t end_nodes = start_way > 0 ? start_way : now;
    time_t end_way = start_rel > 0 ? start_rel : now;
    time_t end_rel = now;
    fprintf(stderr,
            "\rProcessing: Node(%" PRIdOSMID "k %.1fk/s) Way(%" PRIdOSMID "k %.2fk/s) Relation(%" PRIdOSMID " %.2f/s)",
            count_node / 1000,
            (double) count_node / 1000.0 / ((int) (end_nodes - start_node) > 0 ? (double) (end_nodes - start_node) : 1.0),
            count_way / 1000,
            count_way > 0 ? (double) count_way / 1000.0 / ((double) (end_way - start_way) > 0.0 ? (double) (end_way - start_way) : 1.0) : 0.0, count_rel,
            count_rel > 0 ? (double) count_rel / ((double) (end_rel - start_rel) > 0.0 ? (double) (end_rel - start_rel) : 1.0) : 0.0);
}


parse_delegate_t::parse_delegate_t(int extra_attributes,
                                   const boost::optional<std::string> &bbox,
                                   boost::shared_ptr<reprojection> projection,
                                   bool append)
: m_extra_attributes(extra_attributes), m_proj(projection), m_bbox(bbox),
  m_append(append)
{}

parse_delegate_t::~parse_delegate_t() = default;

void parse_delegate_t::stream_file(const std::string &input_reader,
                                 const std::string &filename, osmdata_t *osmdata)
{
    // Each file might need a different kind of parser,
    // so instantiate parser separately for each file.
    auto parser = get_input_reader(input_reader, filename);
    parser->stream_file(filename, osmdata);
    m_stats.update(parser->get_stats());
}

void parse_delegate_t::print_summary() const
{
    m_stats.print_summary();
}

std::unique_ptr<parse_t>
parse_delegate_t::get_input_reader(const std::string &input_reader,
                                   const std::string &filename)
{
    std::string fext;
    if (filename.length() > 3)
       fext = filename.substr(filename.length() - 4, 4);
    if (input_reader == "o5m"
        || (input_reader == "auto" && (fext == ".o5m" || fext == ".o5c")))
        return std::unique_ptr<parse_t>(new parse_o5m_t(m_extra_attributes, m_bbox, m_proj.get()));

    // default is the libosmium parser
    return std::unique_ptr<parse_t>(new parse_osmium_t(input_reader, m_extra_attributes, m_bbox, m_proj.get(), m_append));
}



parse_t::parse_t(int extra_attributes_, const bbox_t &bbox_,
                 const reprojection *projection_)
: node_lon(0), node_lat(0), filetype(FILETYPE_NONE),
  action(ACTION_NONE), parallel_indexing(0),
  extra_attributes(extra_attributes_),  proj(projection_), bbox(bbox_)
{}



