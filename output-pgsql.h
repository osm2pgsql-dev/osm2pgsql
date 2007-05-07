/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 * 
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/
 
#ifndef OUTPUT_PGSQL_H
#define OUTPUT_PGSQL_H

#include "output.h"

struct output_t out_pgsql;

#endif
