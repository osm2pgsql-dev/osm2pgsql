/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
#-----------------------------------------------------------------------------
# Original Python implementation by Artem Pavlenko
# Re-implementation by Jon Burgess, Copyright 2006
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>

#include <zlib.h>
#include <bzlib.h>

#include "osmtypes.h"
#include "output.h"
#include "reprojection.h"

#include "fileformat.pb-c.h"
#include "osmformat.pb-c.h"

#define UNUSED  __attribute__ ((unused))

#define MAX_BLOCK_HEADER_SIZE 64*1024
#define MAX_BLOB_SIZE 32*1024*1024

#define NANO_DEGREE .000000001

static uint32_t get_length(FILE *input)
{
  char buf[4];

  if (1 != fread(buf, sizeof(buf), 1, input))
    return 0;

  return ntohl(*((size_t *)buf));
}

static inline void *realloc_or_free(void *p, size_t len)
{
  void *new = realloc(p, len);

  if (new == NULL) {
    free(p);
  }

  return new;
}

static BlockHeader *read_header(FILE *input, void *buf)
{    
  BlockHeader *header_msg; 
  size_t read, length = get_length(input);
    
  if (length < 1 || length > MAX_BLOCK_HEADER_SIZE) {
    if (!feof(input)) {
      fprintf(stderr, "Invalid blocksize %lu\n", (unsigned long)length);
    }
    return NULL;
  }
  
  read = fread(buf, length, 1, input);
  if (!read) {
    perror("parse-pbf: error while reading header data");
    return NULL;
  }
  
  header_msg = block_header__unpack (NULL, length, buf);
  if (header_msg == NULL) {
    fprintf(stderr, "Error unpacking BlockHeader message\n");
    return NULL;
  }

  return header_msg;
}

static Blob *read_blob(FILE *input, void *buf, int32_t length)
{
  Blob *blob_msg;

  if (length < 1 || length > MAX_BLOB_SIZE) {
    fprintf(stderr, "Blob isn't present or exceeds minimum/maximum size\n");
    return NULL;
  }
  
  if(1 != fread(buf, length, 1, input)) {
    fprintf(stderr, "error reading blob content\n");
    return NULL;
  }
    
  blob_msg = blob__unpack (NULL, length, buf);
  if (blob_msg == NULL) {
    fprintf(stderr, "Error unpacking Blob message\n");
    return NULL;
  }
  
  return blob_msg;
}

static size_t uncompress_blob(Blob *bmsg, void *buf, int32_t max_size) 
{
  if (bmsg->raw_size > max_size) {
    fprintf(stderr, "blob raw size too large\n");
    return 0;
  }
  
  if (bmsg->has_raw) {
    memcpy(buf, bmsg->raw.data, bmsg->raw.len);
    return bmsg->raw.len;
  } else if (bmsg->has_zlib_data) {
    int ret;
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = bmsg->zlib_data.len;
    strm.next_in = bmsg->zlib_data.data;
    strm.avail_out = bmsg->raw_size;
    strm.next_out = buf;

    ret = inflateInit(&strm);
    if (ret != Z_OK) {
      fprintf(stderr, "Zlib init failed\n");
      return 0;
    }

    ret = inflate(&strm, Z_NO_FLUSH);
            
    (void)inflateEnd(&strm);
        
    if (ret != Z_STREAM_END) {
      fprintf(stderr, "Zlib compression failed (code %d, %s)\n", ret, strm.msg);
      return 0;
    }

    return bmsg->raw_size;
  } else if (bmsg->has_bzip2_data) {
    int ret;
    bz_stream strm;
    strm.bzalloc = NULL;
    strm.bzfree = NULL;
    strm.opaque = NULL;
    strm.avail_in = bmsg->bzip2_data.len;
    strm.next_in = (char *) bmsg->bzip2_data.data;
    strm.avail_out = bmsg->raw_size;
    strm.next_out = buf;

    ret = BZ2_bzDecompressInit(&strm, 0, 0);
    if (ret != BZ_OK) {
      fprintf(stderr, "Bzip2 init failed\n");
      return 0;
    }

    (void)BZ2_bzDecompressEnd(&strm);
        
    if (ret != BZ_STREAM_END) {
      fprintf(stderr, "Bzip2 compression failed\n");
      return 0;
    }

    return bmsg->raw_size;
  } else if (bmsg->has_lzma_data) {
    fprintf(stderr, "Can't uncompress LZMA data\n");
    return 0;
  } else {
    fprintf(stderr, "We cannot handle the %d non-raw bytes yet...\n", bmsg->raw_size);
    return 0;
  }
  
  return 0;
}

int addProtobufItem(struct keyval *head, ProtobufCBinaryData key, ProtobufCBinaryData val, int noDupe)
{
  char *keystr, *valstr;
  int retval;

  keystr = calloc(key.len + 1, 1);
  memcpy(keystr, key.data, key.len);

  valstr = calloc(val.len + 1, 1);
  memcpy(valstr, val.data, val.len);
  
  retval = addItem(head, keystr, valstr, noDupe);

  free(keystr);
  free(valstr);

  return retval;
}

int addIntItem(struct keyval *head, const char *key, int val, int noDupe)
{
  char buf[100];

  sprintf(buf, "%d", val);
  return addItem(head, key, buf, noDupe);
}

int addInfoItems(struct keyval *head, Info *info, StringTable *string_table)
{
      if (info->has_version) {
	addIntItem(head, "version", info->version,   0);	    
      }
      if (info->has_changeset) {
	addIntItem(head, "changeset", info->changeset,   0);	    
      }
      if (info->has_uid) {
	addIntItem(head, "uid", info->uid,   0);	    
      }
      if (info->has_user_sid) {
	ProtobufCBinaryData user = string_table->s[info->user_sid];
	char *username;

        username = calloc(user.len + 1, 1);
	memcpy(username, user.data, user.len);

	addItem(head, "user", username, 0);
      }

      /* TODO timestamp */

      return 0;
}

int processOsmHeader(void *data, size_t length)
{
  HeaderBlock *hmsg = header_block__unpack (NULL, length, data);
  if (hmsg == NULL) {
    fprintf(stderr, "Error unpacking HeaderBlock message\n");
    return 0;
  }
  
  header_block__free_unpacked (hmsg, &protobuf_c_system_allocator);

  return 1;
}

int processOsmDataNodes(struct osmdata_t *osmdata, PrimitiveGroup *group, StringTable *string_table, double lat_offset, double lon_offset, double granularity)
{
  for (unsigned node_id = 0; node_id < group->n_nodes; node_id++) {
    Node *node = group->nodes[node_id];
    double lat, lon;

    resetList(&(osmdata->tags));

    if (node->info && osmdata->extra_attributes) {
      addInfoItems(&(osmdata->tags), node->info, string_table);
    }

    for (unsigned key_id = 0; key_id < node->n_keys; key_id++) {
      addProtobufItem(&(osmdata->tags), 
		      string_table->s[node->keys[key_id]], 
		      string_table->s[node->vals[key_id]], 
		      0);
    }

    lat = lat_offset + (node->lat * granularity);
    lon = lon_offset + (node->lon * granularity);
    reproject(&lat, &lon);

    osmdata->out->node_add(node->id, lat, lon, &(osmdata->tags));

    if (node->id > osmdata->max_node) {
      osmdata->max_node = node->id;
    }

	if (osmdata->count_node == 0) {
		time(&osmdata->start_node);
	}
    osmdata->count_node++;
    if (osmdata->count_node%10000 == 0)
      printStatus(osmdata);
  }

  return 1;
}

int processOsmDataDenseNodes(struct osmdata_t *osmdata, PrimitiveGroup *group, StringTable *string_table, double lat_offset, double lon_offset, double granularity)
{
  if (group->dense) {
    unsigned l = 0;
    long int deltaid = 0;
    long int deltalat = 0;
    long int deltalon = 0;
    unsigned long int deltatimestamp = 0;
    unsigned long int deltachangeset = 0;
    long int deltauid = 0;
    unsigned long int deltauser_sid = 0;
    double lat, lon;

    DenseNodes *dense = group->dense;

    for (unsigned node_id = 0; node_id < dense->n_id; node_id++) {
      resetList(&(osmdata->tags));

      deltaid += dense->id[node_id];
      deltalat += dense->lat[node_id];
      deltalon += dense->lon[node_id];

      if (dense->denseinfo && osmdata->extra_attributes) {
	DenseInfo *denseinfo = dense->denseinfo;

	deltatimestamp += denseinfo->timestamp[node_id];
	deltachangeset += denseinfo->changeset[node_id];
	deltauid += denseinfo->uid[node_id];
	deltauser_sid += denseinfo->user_sid[node_id];

	addIntItem(&(osmdata->tags), "version", denseinfo->version[node_id], 0);
	addIntItem(&(osmdata->tags), "changeset", deltachangeset, 0);

#if 0
	/* TODO */
	if (deltauid != -1) { // osmosis devs failed to read the specs
	  printuser(string_table->s[deltauser_sid]);
	  printnumericattribute("uid", deltauid);
	}

	printtimestamp("timestamp", deltatimestamp);
#endif
      }

      if (l < dense->n_keys_vals) {
	while (dense->keys_vals[l] != 0 && l < dense->n_keys_vals) {
	  addProtobufItem(&(osmdata->tags),
			  string_table->s[dense->keys_vals[l]],
			  string_table->s[dense->keys_vals[l+1]],
			  0);

	  l += 2;
	}
	l += 1;
      }

      lat = lat_offset + (deltalat * granularity);
      lon = lon_offset + (deltalon * granularity);
      reproject(&lat, &lon);
      
      osmdata->out->node_add(deltaid, lat, lon, &(osmdata->tags));

      if (deltaid > osmdata->max_node) {
	osmdata->max_node = deltaid;
      }

	  if (osmdata->count_node == 0) {
		time(&osmdata->start_node);
	  }
      osmdata->count_node++;
      if (osmdata->count_node%10000 == 0)
	printStatus(osmdata);
    }
  }

  return 1;
}

int processOsmDataWays(struct osmdata_t *osmdata, PrimitiveGroup *group, StringTable *string_table)
{
  for (unsigned way_id = 0; way_id < group->n_ways; way_id++) {
    Way *way = group->ways[way_id];
    long int deltaref = 0;

    resetList(&(osmdata->tags));

    if (way->info && osmdata->extra_attributes) {
      addInfoItems(&(osmdata->tags), way->info, string_table);
    }

    osmdata->nd_count = 0;

    for (unsigned ref_id = 0; ref_id < way->n_refs; ref_id++) {
      deltaref += way->refs[ref_id];
	    
      osmdata->nds[osmdata->nd_count++] = deltaref;
	    
      if( osmdata->nd_count >= osmdata->nd_max )
	realloc_nodes(osmdata);
    }

    for (unsigned key_id = 0; key_id < way->n_keys; key_id++) {
      addProtobufItem(&(osmdata->tags), 
		      string_table->s[way->keys[key_id]], 
		      string_table->s[way->vals[key_id]], 
		      0);
    }

    osmdata->out->way_add(way->id, 
			  osmdata->nds, 
			  osmdata->nd_count, 
			  &(osmdata->tags) );

    if (way->id > osmdata->max_way) {
      osmdata->max_way = way->id;
    }

	if (osmdata->count_way == 0) {
		time(&osmdata->start_way);
	}
    osmdata->count_way++;
    if (osmdata->count_way%1000 == 0)
      printStatus(osmdata);
  }

  return 1;
}

int processOsmDataRelations(struct osmdata_t *osmdata, PrimitiveGroup *group, StringTable *string_table)
{
  for (unsigned rel_id = 0; rel_id < group->n_relations; rel_id++) {
    Relation *relation = group->relations[rel_id];
    long int deltamemids = 0;

    resetList(&(osmdata->tags));

    osmdata->member_count = 0;

    if (relation->info && osmdata->extra_attributes) {
      addInfoItems(&(osmdata->tags), relation->info, string_table);
    }
	  
    for (unsigned member_id = 0; member_id < relation->n_memids; member_id++) {
      ProtobufCBinaryData role =  string_table->s[relation->roles_sid[member_id]];
      char *rolestr;

      deltamemids += relation->memids[member_id];

      osmdata->members[osmdata->member_count].id = deltamemids;

      rolestr = calloc(role.len + 1, 1);
      memcpy(rolestr, role.data, role.len);
      osmdata->members[osmdata->member_count].role = rolestr;

      switch (relation->types[member_id]) {
      case RELATION__MEMBER_TYPE__NODE:
	osmdata->members[osmdata->member_count].type = OSMTYPE_NODE;
	break;
      case RELATION__MEMBER_TYPE__WAY:
	osmdata->members[osmdata->member_count].type = OSMTYPE_WAY;
	break;
      case RELATION__MEMBER_TYPE__RELATION:
	osmdata->members[osmdata->member_count].type = OSMTYPE_RELATION;
	break;
      default:
	fprintf(stderr, "Unsupported type: %u""\n", relation->types[member_id]);
	return 0;
      }
	    
      osmdata->member_count++;
	  
      if( osmdata->member_count >= osmdata->member_max ) {
	realloc_members(osmdata);
      }
    }

    for (unsigned key_id = 0; key_id < relation->n_keys; key_id++) {
      addProtobufItem(&(osmdata->tags), 
		      string_table->s[relation->keys[key_id]], 
		      string_table->s[relation->vals[key_id]], 
		      0);
    }
	 
    osmdata->out->relation_add(relation->id, 
			       osmdata->members, 
			       osmdata->member_count, 
			       &(osmdata->tags));

    for (unsigned member_id = 0; member_id < osmdata->member_count; member_id++) {
      free(osmdata->members[member_id].role);
    }

    if (relation->id > osmdata->max_rel) {
      osmdata->max_rel = relation->id;
    }

	if (osmdata->count_rel == 0) {
		time(&osmdata->start_rel);
	}
    osmdata->count_rel++;
    if (osmdata->count_rel%10 == 0)
      printStatus(osmdata);
  }

  return 1;
}


int processOsmData(struct osmdata_t *osmdata, void *data, size_t length) 
{
  PrimitiveBlock *pmsg = primitive_block__unpack (NULL, length, data);
  if (pmsg == NULL) {
    fprintf(stderr, "Error unpacking PrimitiveBlock message\n");
    return 0;
  }

  double lat_offset = NANO_DEGREE * pmsg->lat_offset;
  double lon_offset = NANO_DEGREE * pmsg->lon_offset;
  double granularity = NANO_DEGREE * pmsg->granularity;
      

  for (unsigned int j = 0; j < pmsg->n_primitivegroup; j++) {
    PrimitiveGroup *group = pmsg->primitivegroup[j];
    StringTable *string_table = pmsg->stringtable;

    if (!processOsmDataNodes(osmdata, group, string_table, lat_offset, lon_offset, granularity)) return 0;
    if (!processOsmDataDenseNodes(osmdata, group, string_table, lat_offset, lon_offset, granularity)) return 0;
    if (!processOsmDataWays(osmdata, group, string_table)) return 0;
    if (!processOsmDataRelations(osmdata, group, string_table)) return 0;
  }

  primitive_block__free_unpacked (pmsg, &protobuf_c_system_allocator);

  return 1;
}



int streamFilePbf(char *filename, int sanitize UNUSED, struct osmdata_t *osmdata)
{
  void *header = NULL;
  void *blob = NULL;
  char *data = NULL;
  FILE *input = NULL;
  BlockHeader *header_msg = NULL; 
  Blob *blob_msg = NULL;      
  size_t length;
  int exit_status = EXIT_FAILURE;

  header = malloc(MAX_BLOCK_HEADER_SIZE);
  if (!header) {
    fprintf(stderr, "parse-pbf: out of memory allocating header buffer\n");
    goto err;
  }

  blob = malloc(MAX_BLOB_SIZE);
  if (!blob) {
    fprintf(stderr, "parse-pbf: out of memory allocating blob buffer\n");
    goto err;
  }

  data = malloc(MAX_BLOB_SIZE);
  if (!data) {
    fprintf(stderr, "parse-pbf: out of memory allocating data buffer\n");
    goto err;
  }

  input = fopen(filename, "rb");
  if (!input) { 
    fprintf(stderr, "Unable to open %s\n", filename);
    goto err;
  }

  do {
    header_msg = read_header(input, header);
    if (header_msg == NULL) {
      break;
    }

    blob_msg = read_blob(input, blob, header_msg->datasize);

    length = uncompress_blob(blob_msg, data, MAX_BLOB_SIZE);
    if (!length) {
      goto err;
    }

    if (strcmp(header_msg->type, "OSMHeader") == 0) {
      if (!processOsmHeader(data, length)) {
	goto err;
      }
    } else if (strcmp(header_msg->type, "OSMData") == 0) {
      if (!processOsmData(osmdata, data, length)) {
	goto err;
      }
    }

    blob__free_unpacked (blob_msg, &protobuf_c_system_allocator);
    block_header__free_unpacked (header_msg, &protobuf_c_system_allocator);
  } while (!feof(input));

  if (!feof(input)) {
    goto err;
  }

  exit_status = EXIT_SUCCESS;

 err:
  if (input)  fclose(input);

  if (header) free(header);
  if (blob)   free(blob);
  if (data)   free(data);
  
  return exit_status;
}
