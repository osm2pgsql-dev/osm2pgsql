#include "config.h"
#include "parse.hpp"
#include "parse-o5m.hpp"
#ifdef BUILD_READER_PBF
#  include "parse-pbf.hpp"
#endif
#include "parse-xml2.hpp"

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


parse_delegate_t::parse_delegate_t(const int extra_attributes,
                                   const boost::optional<std::string> &bbox,
                                   boost::shared_ptr<reprojection> projection):
m_extra_attributes(extra_attributes), m_proj(projection), m_count_node(0), m_max_node(0),
m_count_way(0), m_max_way(0), m_count_rel(0), m_max_rel(0), m_start_node(0), m_start_way(0), m_start_rel(0)
{
    m_bbox = bool(bbox);
    if (m_bbox)
        parse_bbox(*bbox);
}

parse_delegate_t::~parse_delegate_t()
{
}

int parse_delegate_t::streamFile(const char* input_reader, const char* filename,const int sanitize, osmdata_t *osmdata)
{
	//process the input file with the right parser
	parse_t* parser = get_input_reader(input_reader, filename);
	int ret = parser->streamFile(filename, sanitize, osmdata);

	//update statisics
	m_count_node += parser->count_node;
	m_count_way += parser->count_way;
	m_count_rel += parser->count_rel;
	m_max_node = std::max(parser->max_node, m_max_node);
	m_max_way = std::max(parser->max_way, m_max_way);
	m_max_rel = std::max(parser->max_rel, m_max_rel);
	m_start_node = m_start_node == 0 ? parser->start_node : m_start_node;
	m_start_way = m_start_way == 0 ? parser->start_way : m_start_way;
	m_start_rel = m_start_rel == 0 ? parser->start_rel : m_start_rel;

	//done
	delete parser;

	return ret;
}
void parse_delegate_t::printSummary() const
{
	time_t now = time(NULL);
	time_t end_nodes = m_start_way > 0 ? m_start_way : now;
	time_t end_way = m_start_rel > 0 ? m_start_rel : now;
	time_t end_rel = now;

	fprintf(stderr,
			"Node stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
			m_count_node, m_max_node,
			m_count_node > 0 ? (int) (end_nodes - m_start_node) : 0);
	fprintf(stderr,
			"Way stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
			m_count_way, m_max_way,
			m_count_way > 0 ? (int) (end_way - m_start_way) : 0);
	fprintf(stderr,
			"Relation stats: total(%" PRIdOSMID "), max(%" PRIdOSMID ") in %is\n",
			m_count_rel, m_max_rel,
			m_count_rel > 0 ? (int) (end_rel - m_start_rel) : 0);
}

boost::shared_ptr<reprojection> parse_delegate_t::getProjection() const
{
	return m_proj;
}

void parse_delegate_t::parse_bbox(const std::string &bbox_)
{
    int n = sscanf(bbox_.c_str(), "%lf,%lf,%lf,%lf", &(m_minlon), &(m_minlat), &(m_maxlon), &(m_maxlat));
    if (n != 4)
        throw std::runtime_error("Bounding box must be specified like: minlon,minlat,maxlon,maxlat\n");

    if (m_maxlon <= m_minlon)
        throw std::runtime_error("Bounding box failed due to maxlon <= minlon\n");

    if (m_maxlat <= m_minlat)
        throw std::runtime_error("Bounding box failed due to maxlat <= minlat\n");

    fprintf(stderr, "Applying Bounding box: %f,%f to %f,%f\n", m_minlon, m_minlat, m_maxlon, m_maxlat);
}

parse_t* parse_delegate_t::get_input_reader(const char* input_reader, const char* filename)
{
	// if input_reader is forced to a specific iput format
	if (strcmp("auto", input_reader) != 0) {
		if (strcmp("libxml2", input_reader) == 0) {
			return new parse_xml2_t(m_extra_attributes, m_bbox, m_proj, m_minlon, m_minlat, m_maxlon, m_maxlat);
		} else if (strcmp("primitive", input_reader) == 0) {
      // The more robust libxml2 parser can be used instead of primitive
			return new parse_xml2_t(m_extra_attributes, m_bbox, m_proj, m_minlon, m_minlat, m_maxlon, m_maxlat);
#ifdef BUILD_READER_PBF
		} else if (strcmp("pbf", input_reader) == 0) {
			return new parse_pbf_t(m_extra_attributes, m_bbox, m_proj, m_minlon, m_minlat, m_maxlon, m_maxlat);
#endif
		} else if (strcmp("o5m", input_reader) == 0) {
			return new parse_o5m_t(m_extra_attributes, m_bbox, m_proj, m_minlon, m_minlat, m_maxlon, m_maxlat);
		} else {
			fprintf(stderr, "Input parser `%s' not recognised. Should be one of [libxml2, o5m"
#ifdef BUILD_READER_PBF
							", pbf"
#endif
							"].\n", input_reader);
			exit(EXIT_FAILURE);
		}
	} // if input_reader is not forced by -r switch try to auto-detect it by file extension
	else {
		if (strcasecmp(".pbf", filename + strlen(filename) - 4) == 0) {
#ifdef BUILD_READER_PBF
			return new parse_pbf_t(m_extra_attributes, m_bbox, m_proj, m_minlon, m_minlat, m_maxlon, m_maxlat);
#else
			fprintf(stderr, "ERROR: PBF support has not been compiled into this version of osm2pgsql, please either compile it with pbf support or use one of the other input formats\n");
			exit(EXIT_FAILURE);
#endif
		} else if (strcasecmp(".o5m", filename + strlen(filename) - 4) == 0
				|| strcasecmp(".o5c", filename + strlen(filename) - 4) == 0) {
			return new parse_o5m_t(m_extra_attributes, m_bbox, m_proj, m_minlon, m_minlat, m_maxlon, m_maxlat);
		} else {
			return new parse_xml2_t(m_extra_attributes, m_bbox, m_proj, m_minlon, m_minlat, m_maxlon, m_maxlat);
		}
	}
}



parse_t::parse_t(const int extra_attributes_, const bool bbox_, const boost::shared_ptr<reprojection>& projection_,
	const double minlon_, const double minlat_, const double maxlon_, const double maxlat_):
		extra_attributes(extra_attributes_), bbox(bbox_), minlon(minlon_), minlat(minlat_),
		maxlon(maxlon_), maxlat(maxlat_), proj(projection_)
{
	osm_id = count_node = max_node = count_way = max_way = count_rel = 0;
	max_rel = parallel_indexing = start_node = start_way = start_rel = 0;
	node_lon = node_lat = 0;

	filetype = FILETYPE_NONE;
	action   = ACTION_NONE;
}

parse_t::~parse_t()
{
}


void parse_t::print_status()
{
	time_t now = time(NULL);
	time_t end_nodes = start_way > 0 ? start_way : now;
	time_t end_way = start_rel > 0 ? start_rel : now;
	time_t end_rel = now;
	fprintf(stderr,
			"\rProcessing: Node(%" PRIdOSMID "k %.1fk/s) Way(%" PRIdOSMID "k %.2fk/s) Relation(%" PRIdOSMID " %.2f/s)",
			count_node / 1000,
			(double) count_node / 1000.0 / ((int) (end_nodes - start_node) > 0 ? (double) (end_nodes - start_node) : 1.0),
			count_way / 1000,
			count_way > 0 ?	(double) count_way / 1000.0	/ ((double) (end_way - start_way) > 0.0 ? (double) (end_way - start_way) : 1.0) : 0.0, count_rel,
			count_rel > 0 ?	(double) count_rel / ((double) (end_rel - start_rel) > 0.0 ? (double) (end_rel - start_rel) : 1.0) : 0.0);
}


