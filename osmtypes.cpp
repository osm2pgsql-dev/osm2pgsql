#include <stdlib.h>
#include <stdio.h>
#include "osmtypes.hpp"

#define INIT_MAX_MEMBERS 64
#define INIT_MAX_NODES  4096

void realloc_nodes(struct osmdata_t *osmdata)
{
  if( osmdata->nd_max == 0 )
    osmdata->nd_max = INIT_MAX_NODES;
  else
    osmdata->nd_max <<= 1;
    
  osmdata->nds = (osmid_t *)realloc( osmdata->nds, osmdata->nd_max * sizeof( osmdata->nds[0] ) );
  if( !osmdata->nds )
  {
    fprintf( stderr, "Failed to expand node list to %d\n", osmdata->nd_max );
    exit_nicely();
  }
}

void realloc_members(struct osmdata_t *osmdata)
{
  if( osmdata->member_max == 0 )
    osmdata->member_max = INIT_MAX_NODES;
  else
    osmdata->member_max <<= 1;
    
  osmdata->members = (struct member *)realloc( osmdata->members, osmdata->member_max * sizeof( osmdata->members[0] ) );
  if( !osmdata->members )
  {
    fprintf( stderr, "Failed to expand member list to %d\n", osmdata->member_max );
    exit_nicely();
  }
}

void resetMembers(struct osmdata_t *osmdata)
{
  unsigned i;
  for(i = 0; i < osmdata->member_count; i++ )
    free( osmdata->members[i].role );
}

void printStatus(struct osmdata_t *osmdata)
{
    time_t now;
    time_t end_nodes;
    time_t end_way;
    time_t end_rel;
    time(&now);
    end_nodes = osmdata->start_way > 0 ? osmdata->start_way : now;
    end_way = osmdata->start_rel > 0 ? osmdata->start_rel : now;
    end_rel =  now;
    fprintf(stderr, "\rProcessing: Node(%" PRIdOSMID "k %.1fk/s) Way(%" PRIdOSMID "k %.2fk/s) Relation(%" PRIdOSMID " %.2f/s)",
            osmdata->count_node/1000,
            (double)osmdata->count_node/1000.0/((int)(end_nodes - osmdata->start_node) > 0 ? (double)(end_nodes - osmdata->start_node) : 1.0),
            osmdata->count_way/1000,
            osmdata->count_way > 0 ? (double)osmdata->count_way/1000.0/
            ((double)(end_way - osmdata->start_way) > 0.0 ? (double)(end_way - osmdata->start_way) : 1.0) : 0.0,
            osmdata->count_rel,
            osmdata->count_rel > 0 ? (double)osmdata->count_rel/
            ((double)(end_rel - osmdata->start_rel) > 0.0 ? (double)(end_rel - osmdata->start_rel) : 1.0) : 0.0);
}

int node_wanted(struct osmdata_t *osmdata, double lat, double lon)
{
    if (!osmdata->bbox)
        return 1;

    if (lat < osmdata->minlat || lat > osmdata->maxlat)
        return 0;
    if (lon < osmdata->minlon || lon > osmdata->maxlon)
        return 0;
    return 1;
}

