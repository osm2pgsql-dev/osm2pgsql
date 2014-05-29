#ifndef PARSE_H
#define PARSE_H

#include <time.h>
#include <config.h>

#include "keyvals.hpp"
#include "reprojection.hpp"
#include "osmtypes.hpp"

#include <boost/shared_ptr.hpp>

typedef enum { FILETYPE_NONE, FILETYPE_OSM, FILETYPE_OSMCHANGE, FILETYPE_PLANETDIFF } filetypes_t;
typedef enum { ACTION_NONE, ACTION_CREATE, ACTION_MODIFY, ACTION_DELETE } actions_t;

class parse_t;

class parse_delegate_t
{
public:
	parse_delegate_t(const int extra_attributes, const char* bbox, boost::shared_ptr<reprojection> projection);
	~parse_delegate_t();

	int streamFile(const char* input_reader, const char* filename, const int sanitize, osmdata_t *osmdata);
	void printSummary() const;
	boost::shared_ptr<reprojection> getProjection() const;

private:
	parse_delegate_t();
	void parse_bbox(const char* bbox);
	parse_t* get_input_reader(const char* input_reader, const char* filename);

	osmid_t m_count_node, m_max_node;
	osmid_t m_count_way,  m_max_way;
	osmid_t m_count_rel,  m_max_rel;
	time_t  m_start_node, m_start_way, m_start_rel;

	const int m_extra_attributes;
	boost::shared_ptr<reprojection> m_proj;
	bool m_bbox;
	double m_minlon, m_minlat, m_maxlon, m_maxlat;
	keyval m_tags;
};

class parse_t
{
	friend class parse_delegate_t;

public:
	parse_t(const int extra_attributes_, const bool bbox_, const boost::shared_ptr<reprojection>& projection_,
			const double minlon, const double minlat, const double maxlon, const double maxlat, keyval& tags_);
	virtual ~parse_t();

	virtual int streamFile(const char *filename, const int sanitize, osmdata_t *osmdata) = 0;

protected:
	parse_t();

	virtual void realloc_nodes();
	virtual void realloc_members();
	virtual void resetMembers();
	virtual void printStatus();
	virtual int node_wanted(double lat, double lon);

	osmid_t count_node,    max_node;
	osmid_t count_way,     max_way;
	osmid_t count_rel,     max_rel;
	time_t  start_node, start_way, start_rel;

	/* Since {node,way} elements are not nested we can guarantee the
	values in an end tag must match those of the corresponding
	start tag and can therefore be cached.
	*/
	double node_lon, node_lat;
	keyval& tags;
	osmid_t *nds;
	int nd_count, nd_max;
	member *members;
	int member_count, member_max;
	osmid_t osm_id;
	filetypes_t filetype;
	actions_t action;
	int parallel_indexing;

	const int extra_attributes;
	mutable bool bbox;
	const boost::shared_ptr<reprojection> proj;
	mutable double minlon, minlat, maxlon, maxlat;
};

#endif
