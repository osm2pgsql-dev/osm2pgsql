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

#include "config.h"

#ifdef BUILD_READER_PBF

#include <cassert>
#include <cstdlib>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <time.h>

#include <zlib.h>
#include <string>

#include "parse-pbf.hpp"
#include "osmdata.hpp"
#include "osmtypes.hpp"
#include "reprojection.hpp"

#define MAX_BLOCK_HEADER_SIZE 64*1024
#define MAX_BLOB_SIZE 32*1024*1024

#define NANO_DEGREE .000000001

static uint32_t get_length(FILE *input)
{
  uint32_t buf;

  if (1 != fread(reinterpret_cast<char *>(&buf), sizeof(buf), 1, input))
    return 0;

  return ntohl(buf);
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

  header_msg = block_header__unpack (NULL, length, (const uint8_t *)buf);
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

  blob_msg = blob__unpack (NULL, length, (const uint8_t *)buf);
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
    strm.next_out = (Bytef *)buf;

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
    fprintf(stderr, "Can't uncompress bz2 data\n");
    return 0;
  } else if (bmsg->has_lzma_data) {
    fprintf(stderr, "Can't uncompress LZMA data\n");
    return 0;
  } else {
    fprintf(stderr, "We cannot handle the %d non-raw bytes yet...\n", bmsg->raw_size);
    return 0;
  }

  return 0;
}

void parse_pbf_t::addProtobufItem(ProtobufCBinaryData &key,
                                  ProtobufCBinaryData &val)
{
  std::string keystr((const char *) key.data, key.len);
  assert(keystr.find('\0') == std::string::npos);

  std::string valstr((const char *) val.data, val.len);
  assert(valstr.find('\0') == std::string::npos);

  tags.push_back(tag(keystr, valstr));
}

void parse_pbf_t::addIntItem(const char *key, int val)
{
  char buf[100];
  sprintf(buf, "%d", val);

  tags.push_back(tag(key, buf));
}

void parse_pbf_t::addInfoItems(Info *info, StringTable *string_table)
{
  if (info->has_version)
    addIntItem("osm_version", info->version);

  if (info->has_changeset)
    addIntItem("osm_changeset", info->changeset);

  if (info->has_uid)
    addIntItem("osm_uid", info->uid);

  if (info->has_user_sid) {
    ProtobufCBinaryData user = string_table->s[info->user_sid];

    tags.push_back(tag("osm_user", std::string((const char *) user.data, user.len)));
  }

  /* TODO timestamp */
}

static int processOsmHeader(void *data, size_t length)
{
  HeaderBlock *hmsg = header_block__unpack (NULL, length, (const uint8_t *)data);
  if (hmsg == NULL) {
    fprintf(stderr, "Error unpacking HeaderBlock message\n");
    return 0;
  }

  header_block__free_unpacked (hmsg, NULL);

  return 1;
}

int parse_pbf_t::processOsmDataNodes(osmdata_t *osmdata, PrimitiveGroup *group,
                                     StringTable *string_table, double lat_offset,
                                     double lon_offset, double granularity)
{
  for (unsigned node_id = 0; node_id < group->n_nodes; node_id++) {
    Node *node = group->nodes[node_id];

    tags.clear();

    if (node->info && extra_attributes) {
      addInfoItems(node->info, string_table);
    }

    for (unsigned key_id = 0; key_id < node->n_keys; key_id++) {
      addProtobufItem(string_table->s[node->keys[key_id]],
                      string_table->s[node->vals[key_id]]);
    }

    double lat = lat_offset + (node->lat * granularity);
    double lon = lon_offset + (node->lon * granularity);
    if (node_wanted(lat, lon)) {
        proj->reproject(&lat, &lon);

        osmdata->node_add(node->id, lat, lon, tags);

        if (node->id > max_node) {
            max_node = node->id;
        }

        if (count_node == 0) {
            time(&start_node);
        }
        count_node++;
        if (count_node%10000 == 0)
            print_status();
    }
  }

  return 1;
}

int parse_pbf_t::processOsmDataDenseNodes(osmdata_t *osmdata, PrimitiveGroup *group,
                                          StringTable *string_table,
                                          double lat_offset, double lon_offset,
                                          double granularity)
{
  if (!group->dense)
    return 1;

  unsigned l = 0;
  osmid_t deltaid = 0;
  long int deltalat = 0;
  long int deltalon = 0;
  unsigned long int deltatimestamp = 0;
  unsigned long int deltachangeset = 0;
  long int deltauid = 0;
  unsigned long int deltauser_sid = 0;

  DenseNodes *dense = group->dense;

  for (unsigned node_id = 0; node_id < dense->n_id; node_id++) {
      tags.clear();

      deltaid += dense->id[node_id];
      deltalat += dense->lat[node_id];
      deltalon += dense->lon[node_id];

      if (dense->denseinfo && extra_attributes) {
          DenseInfo *denseinfo = dense->denseinfo;

          deltatimestamp += denseinfo->timestamp[node_id];
          deltachangeset += denseinfo->changeset[node_id];
          deltauid += denseinfo->uid[node_id];
          deltauser_sid += denseinfo->user_sid[node_id];

          addIntItem("osm_version", denseinfo->version[node_id]);
          addIntItem("osm_changeset", deltachangeset);

          if (deltauid != -1) { /* osmosis devs failed to read the specs */
              addIntItem("osm_uid", deltauid);
              tags.push_back(tag("osm_user",
                                 std::string((const char *) string_table->s[deltauser_sid].data,
                                             string_table->s[deltauser_sid].len)));
          }
      }

      if (l < dense->n_keys_vals) {
          while (dense->keys_vals[l] != 0 && l < dense->n_keys_vals) {
              addProtobufItem(string_table->s[dense->keys_vals[l]],
                              string_table->s[dense->keys_vals[l+1]]);

              l += 2;
          }
          l += 1;
      }

      double lat = lat_offset + (deltalat * granularity);
      double lon = lon_offset + (deltalon * granularity);
      if (node_wanted(lat, lon)) {
          proj->reproject(&lat, &lon);

          osmdata->node_add(deltaid, lat, lon, tags);

          if (deltaid > max_node) {
              max_node = deltaid;
          }

          if (count_node == 0) {
              time(&start_node);
          }
          count_node++;
          if (count_node%10000 == 0)
              print_status();
      }
  }

  return 1;
}

int parse_pbf_t::processOsmDataWays(osmdata_t *osmdata, PrimitiveGroup *group,
                                    StringTable *string_table)
{
  for (unsigned way_id = 0; way_id < group->n_ways; way_id++) {
    Way *way = group->ways[way_id];
    osmid_t deltaref = 0;

    tags.clear();

    if (way->info && extra_attributes) {
      addInfoItems(way->info, string_table);
    }

    nds.clear();

    for (unsigned ref_id = 0; ref_id < way->n_refs; ref_id++) {
      deltaref += way->refs[ref_id];

      nds.push_back(deltaref);
    }

    for (unsigned key_id = 0; key_id < way->n_keys; key_id++) {
      addProtobufItem(string_table->s[way->keys[key_id]],
                      string_table->s[way->vals[key_id]]);
    }

    osmdata->way_add(way->id, nds, tags);

    if (way->id > max_way) {
      max_way = way->id;
    }

    if (count_way == 0) {
        time(&start_way);
    }
    count_way++;
    if (count_way%1000 == 0)
      print_status();
  }

  return 1;
}

int parse_pbf_t::processOsmDataRelations(osmdata_t *osmdata, PrimitiveGroup *group,
                                         StringTable *string_table)
{
  for (unsigned rel_id = 0; rel_id < group->n_relations; rel_id++) {
    Relation *relation = group->relations[rel_id];
    osmid_t deltamemids = 0;

    tags.clear();
    members.clear();

    if (relation->info && extra_attributes) {
      addInfoItems(relation->info, string_table);
    }

    for (unsigned member_id = 0; member_id < relation->n_memids; member_id++) {
      ProtobufCBinaryData role =  string_table->s[relation->roles_sid[member_id]];

      deltamemids += relation->memids[member_id];

      OsmType type;
      switch (relation->types[member_id]) {
        case RELATION__MEMBER_TYPE__NODE: type = OSMTYPE_NODE; break;
        case RELATION__MEMBER_TYPE__WAY: type = OSMTYPE_WAY; break;
        case RELATION__MEMBER_TYPE__RELATION: type = OSMTYPE_RELATION; break;
        default:
          fprintf(stderr, "Unsupported type: %u""\n", relation->types[member_id]);
          return 0;
      }

      members.push_back(member(type, deltamemids,
                               std::string((const char *)role.data, role.len)));
    }

    for (unsigned key_id = 0; key_id < relation->n_keys; key_id++) {
      addProtobufItem(string_table->s[relation->keys[key_id]],
                      string_table->s[relation->vals[key_id]]);
    }

    osmdata->relation_add(relation->id, members, tags);

    if (relation->id > max_rel) {
      max_rel = relation->id;
    }

    if (count_rel == 0) {
        time(&start_rel);
    }
    count_rel++;
    if (count_rel%10 == 0)
      print_status();
  }

  return 1;
}


int parse_pbf_t::processOsmData(osmdata_t *osmdata, void *data, size_t length)
{
  unsigned int j;
  double lat_offset, lon_offset, granularity;
  PrimitiveBlock *pmsg = primitive_block__unpack (NULL, length, (const uint8_t *)data);
  if (pmsg == NULL) {
    fprintf(stderr, "Error unpacking PrimitiveBlock message\n");
    return 0;
  }

  lat_offset = NANO_DEGREE * pmsg->lat_offset;
  lon_offset = NANO_DEGREE * pmsg->lon_offset;
  granularity = NANO_DEGREE * pmsg->granularity;

  for (j = 0; j < pmsg->n_primitivegroup; j++) {
    PrimitiveGroup *group = pmsg->primitivegroup[j];
    StringTable *string_table = pmsg->stringtable;

    if (!processOsmDataNodes(osmdata, group, string_table, lat_offset, lon_offset, granularity)) return 0;
    if (!processOsmDataDenseNodes(osmdata, group, string_table, lat_offset, lon_offset, granularity)) return 0;
    if (!processOsmDataWays(osmdata, group, string_table)) return 0;
    if (!processOsmDataRelations(osmdata, group, string_table)) return 0;
  }

  primitive_block__free_unpacked (pmsg, NULL);

  return 1;
}

parse_pbf_t::parse_pbf_t(const int extra_attributes_, const bool bbox_, const boost::shared_ptr<reprojection>& projection_,
		const double minlon, const double minlat, const double maxlon, const double maxlat):
		parse_t(extra_attributes_, bbox_, projection_, minlon, minlat, maxlon, maxlat)
{

}

parse_pbf_t::~parse_pbf_t()
{

}

int parse_pbf_t::streamFile(const char *filename, const int, osmdata_t *osmdata)
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

  data = (char *)malloc(MAX_BLOB_SIZE);
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

    blob__free_unpacked (blob_msg, NULL);
    block_header__free_unpacked (header_msg, NULL);
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
#endif //BUILD_READER_PBF
