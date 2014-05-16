#include <stdlib.h>
#include <stdio.h>
#include <stdexcept>
#include "osmtypes.hpp"
#include "output.hpp"

#define INIT_MAX_MEMBERS 64
#define INIT_MAX_NODES  4096

osmdata_t::osmdata_t(output_t* out_, const int& extra_attributes_, const char* bbox_, int projection)
{
	minlon = minlat = maxlon = maxlat = osm_id = nd_max = 0;
	count_node = max_node = count_way = max_way = count_rel = max_rel = 0;
	parallel_indexing = start_node = start_way = start_rel = 0;
	nd_count = nd_max = node_lon = node_lat = member_count = member_max = 0;
	members = NULL;
	nds = NULL;
	bbox = false;

	filetype = FILETYPE_NONE;
	action   = ACTION_NONE;

	out = out_;
	extra_attributes = extra_attributes_;
	bbox = bbox_ != NULL;
	parse_bbox(bbox_);
	initList(&tags);
	proj.reset(new reprojection(projection));

	realloc_nodes();
	realloc_members();
}

osmdata_t::~osmdata_t()
{
	if(nds != NULL)
		free(nds);
	if(members != NULL)
		free(members);
}

void osmdata_t::parse_bbox(const char* bbox_)
{
	//bounding box is optional
	if (bbox_)
	{
		int n = sscanf(bbox_, "%lf,%lf,%lf,%lf", &(minlon), &(minlat), &(maxlon), &(maxlat));
		if (n != 4)
			throw std::runtime_error("Bounding box must be specified like: minlon,minlat,maxlon,maxlat\n");

		if (maxlon <= minlon)
			throw std::runtime_error("Bounding box failed due to maxlon <= minlon\n");

		if (maxlat <= minlat)
			throw std::runtime_error("Bounding box failed due to maxlat <= minlat\n");

		fprintf(stderr, "Applying Bounding box: %f,%f to %f,%f\n", minlon, minlat, maxlon, maxlat);
	}
}

void osmdata_t::realloc_nodes()
{
  if( nd_max == 0 )
    nd_max = INIT_MAX_NODES;
  else
    nd_max <<= 1;
    
  nds = (osmid_t *)realloc( nds, nd_max * sizeof( nds[0] ) );
  if( !nds )
  {
    fprintf( stderr, "Failed to expand node list to %d\n", nd_max );
    exit_nicely();
  }
}

void osmdata_t::realloc_members()
{
  if( member_max == 0 )
    member_max = INIT_MAX_NODES;
  else
    member_max <<= 1;
    
  members = (struct member *)realloc( members, member_max * sizeof( members[0] ) );
  if( !members )
  {
    fprintf( stderr, "Failed to expand member list to %d\n", member_max );
    exit_nicely();
  }
}

void osmdata_t::resetMembers()
{
  unsigned i;
  for(i = 0; i < member_count; i++ )
    free( members[i].role );
}

void osmdata_t::printStatus()
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

void osmdata_t::printSummary()
{
	time_t now = time(NULL);
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

int osmdata_t::node_wanted(double lat, double lon)
{
    if (!bbox)
        return 1;

    if (lat < minlat || lat > maxlat)
        return 0;
    if (lon < minlon || lon > maxlon)
        return 0;
    return 1;
}
