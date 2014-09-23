/* Implements the mid-layer processing for osm2pgsql
 * using several arrays in RAM. This is fastest if you
 * have sufficient RAM+Swap.
 *
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libpq-fe.h>

#include <stdexcept>

#include "osmtypes.hpp"
#include "middle-ram.hpp"
#include "node-ram-cache.hpp"
#include "output-pgsql.hpp"
#include "options.hpp"
#include "util.hpp"

/* Store +-20,000km Mercator co-ordinates as fixed point 32bit number with maximum precision */
/* Scale is chosen such that 40,000 * SCALE < 2^32          */
#define FIXED_POINT

/* Object storage now uses 2 levels of storage arrays.
 *
 * - Low level storage of 2^16 (~65k) objects in an indexed array
 *   These are allocated dynamically when we need to first store data with
 *   an ID in this block
 *
 * - Fixed array of 2^(32 - 16) = 65k pointers to the dynamically allocated arrays.
 *
 * This allows memory usage to be efficient and scale dynamically without needing to
 * hard code maximum IDs. We now support an ID  range of -2^31 to +2^31.
 * The negative IDs often occur in non-uploaded JOSM data or other data import scripts.
 *
 */

#define BLOCK_SHIFT 10
#define PER_BLOCK  (1 << BLOCK_SHIFT)
#define NUM_BLOCKS (1 << (32 - BLOCK_SHIFT))

static osmid_t id2block(osmid_t id)
{
    /* + NUM_BLOCKS/2 allows for negative IDs */
    return (id >> BLOCK_SHIFT) + NUM_BLOCKS/2;
}

static  osmid_t id2offset(osmid_t id)
{
    return id & (PER_BLOCK-1);
}

static int block2id(int block, int offset)
{
    return ((block - NUM_BLOCKS/2) << BLOCK_SHIFT) + offset;
}

#define UNUSED  __attribute__ ((unused))

int middle_ram_t::nodes_set(osmid_t id, double lat, double lon, struct keyval *tags) {
    return cache->set(id, lat, lon, tags);
}

int middle_ram_t::ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags)
{
    int pending = 0;
    int block  = id2block(id);
    int offset = id2offset(id);
    struct keyval *p;

    if (!ways[block]) {
        ways[block] = (struct ramWay *)calloc(PER_BLOCK, sizeof(struct ramWay));
        if (!ways[block]) {
            fprintf(stderr, "Error allocating ways\n");
            util::exit_nicely();
        }
        way_blocks++;
    }

    if (ways[block][offset].ndids) {
        free(ways[block][offset].ndids);
        ways[block][offset].ndids = NULL;
    }

    /* Copy into length prefixed array */
    ways[block][offset].ndids = (osmid_t *)malloc( (nd_count+1)*sizeof(osmid_t) );
    memcpy( ways[block][offset].ndids+1, nds, nd_count*sizeof(osmid_t) );
    ways[block][offset].ndids[0] = nd_count;
    ways[block][offset].pending = pending;

    if (!ways[block][offset].tags) {
        p = (struct keyval *)malloc(sizeof(struct keyval));
        if (p) {
            initList(p);
            ways[block][offset].tags = p;
        } else {
            fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
            util::exit_nicely();
        }
    } else
        resetList(ways[block][offset].tags);

    cloneList(ways[block][offset].tags, tags);

    return 0;
}

int middle_ram_t::relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
    struct keyval *p;
    struct member *ptr;
    int block  = id2block(id);
    int offset = id2offset(id);
    if (!rels[block]) {
        rels[block] = (struct ramRel *)calloc(PER_BLOCK, sizeof(struct ramRel));
        if (!rels[block]) {
            fprintf(stderr, "Error allocating rels\n");
            util::exit_nicely();
        }
    }

    if (!rels[block][offset].tags) {
        p = (struct keyval *)malloc(sizeof(struct keyval));
        if (p) {
            initList(p);
            rels[block][offset].tags = p;
        } else {
            fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
            util::exit_nicely();
        }
    } else
        resetList(rels[block][offset].tags);

    cloneList(rels[block][offset].tags, tags);

    if (!rels[block][offset].members)
      free( rels[block][offset].members );

    ptr = (struct member *)malloc(sizeof(struct member) * member_count);
    if (ptr) {
        memcpy( ptr, members, sizeof(struct member) * member_count );
        rels[block][offset].member_count = member_count;
        rels[block][offset].members = ptr;
    } else {
        fprintf(stderr, "%s malloc failed\n", __FUNCTION__);
        util::exit_nicely();
    }

    return 0;
}

int middle_ram_t::nodes_get_list(struct osmNode *nodes, const osmid_t *ndids, int nd_count) const
{
    int i, count;

    count = 0;
    for( i=0; i<nd_count; i++ )
    {
        if (cache->get(&nodes[count], ndids[i]))
            continue;

        count++;
    }
    return count;
}

void middle_ram_t::iterate_relations(middle_t::cb_func &callback)
{
    int block, offset;

    // to maintain backwards compatibility, we need to set this flag
    // which fakes the previous behaviour of having deleted all the
    // ways.
    simulate_ways_deleted = true;

    fprintf(stderr, "\n");
    for(block=NUM_BLOCKS-1; block>=0; block--) {
        if (!rels[block])
            continue;

        for (offset=0; offset < PER_BLOCK; offset++) {
            if (rels[block][offset].members) {
                osmid_t id = block2id(block, offset);
                rel_out_count++;
                if (rel_out_count % 10 == 0)
                    fprintf(stderr, "\rWriting relation (%u)", rel_out_count);

                callback(id, 0);
            }
        }
    }

    fprintf(stderr, "\rWriting relation (%u)\n", rel_out_count);
}

size_t middle_ram_t::pending_count() const {
    //TODO: keep a running count of marked pending stuff
    //so we dont have to iterate over the memory to know
    return 42;
}

void middle_ram_t::iterate_ways(middle_t::pending_processor& pf)
{
    int block, offset;

    fprintf(stderr, "\n");
    for(block=NUM_BLOCKS-1; block>=0; block--) {
        if (!ways[block])
            continue;

        for (offset=0; offset < PER_BLOCK; offset++) {
            if (ways[block][offset].ndids) {
                way_out_count++;
                if (way_out_count % 1000 == 0)
                    fprintf(stderr, "\rEnqueuing way (%uk)", way_out_count/1000);

                if (ways[block][offset].pending) {
                    /* First element contains number of nodes */
                    if (ways[block][offset].ndids[0]) {
                        osmid_t id = block2id(block, offset);
                        pf.enqueue(id);
                    }

                    ways[block][offset].pending = 0;
                }
            }
        }
    }
    fprintf(stderr, "\rEnqueuing way (%uk)\n", way_out_count/1000);

    //let the threads process the ways
    pf.process_ways();

    //TODO: message to show the real progress of writing the ways
}

void middle_ram_t::release_relations()
{
    int block, offset;

    for(block=NUM_BLOCKS-1; block>=0; block--) {
        if (!rels[block])
            continue;

        for (offset=0; offset < PER_BLOCK; offset++) {
            if (rels[block][offset].members) {
                free(rels[block][offset].members);
                rels[block][offset].members = NULL;
                resetList(rels[block][offset].tags);
                free(rels[block][offset].tags);
                rels[block][offset].tags=NULL;
            }
        }
        free(rels[block]);
        rels[block] = NULL;
    }
}

void middle_ram_t::release_ways()
{
    int i, j = 0;

    for (i=0; i<NUM_BLOCKS; i++) {
        if (ways[i]) {
            for (j=0; j<PER_BLOCK; j++) {
                if (ways[i][j].tags) {
                    resetList(ways[i][j].tags);
                    free(ways[i][j].tags);
                }
                if (ways[i][j].ndids)
                    free(ways[i][j].ndids);
            }
            free(ways[i]);
            ways[i] = NULL;
        }
    }
}

/* Caller must free nodes_ptr and resetList(tags_ptr) */
int middle_ram_t::ways_get(osmid_t id, struct keyval *tags_ptr, struct osmNode **nodes_ptr, int *count_ptr) const
{
    int block = id2block(id), offset = id2offset(id), ndCount = 0;
    struct osmNode *nodes;

    if (simulate_ways_deleted)
        return 1;

    if (!ways[block])
        return 1;

    if (ways[block][offset].ndids) {
        /* First element contains number of nodes */
        nodes = (struct osmNode *)malloc( sizeof(struct osmNode) * ways[block][offset].ndids[0]);
        ndCount = nodes_get_list(nodes, ways[block][offset].ndids+1, ways[block][offset].ndids[0]);

        if (ndCount) {
            cloneList( tags_ptr, ways[block][offset].tags );
            *nodes_ptr = nodes;
            *count_ptr = ndCount;
            return 0;
        }
        free(nodes);
    }
    return 1;
}

int middle_ram_t::ways_get_list(const osmid_t *ids, int way_count, osmid_t *way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const {
    int count = 0;
    int i;

    initList(&(tag_ptr[count]));
    for (i = 0; i < way_count; i++) {
        
        if (ways_get(ids[i], &(tag_ptr[count]), &(node_ptr[count]), &(count_ptr[count])) == 0) {
            way_ids[count] = ids[i];
            count++;
            initList(&(tag_ptr[count]));
        }
    }
    return count;
}

/* Caller must free members_ptr and resetList(tags_ptr).
 * Note that the members in members_ptr are copied, but the roles
 * within the members are not, and should not be freed.
 */
int middle_ram_t::relations_get(osmid_t id, struct member **members_ptr, int *member_count, struct keyval *tags_ptr) const
{
    int block = id2block(id), offset = id2offset(id), ndCount = 0;
    struct member *members;

    if (!rels[block])
        return 1;

    if (rels[block][offset].members) {
        const size_t member_bytes = sizeof(struct member) * rels[block][offset].member_count;
        members = (struct member *)malloc(member_bytes);
        memcpy(members, rels[block][offset].members, member_bytes);
        cloneList( tags_ptr, rels[block][offset].tags );

        *members_ptr = members;
        *member_count = rels[block][offset].member_count;
        return 0;
    }
    return 1;
}

void middle_ram_t::analyze(void)
{
    /* No need */
}

void middle_ram_t::cleanup(void)
{
    /* No need */
}

void middle_ram_t::end(void)
{
    /* No need */
}

int middle_ram_t::start(const options_t *out_options_)
{
    out_options = out_options_;
    /* latlong has a range of +-180, mercator +-20000
       The fixed poing scaling needs adjusting accordingly to
       be stored accurately in an int */
    cache.reset(new node_ram_cache(out_options->alloc_chunkwise, out_options->cache, out_options->scale));
    
    fprintf( stderr, "Mid: Ram, scale=%d\n", out_options->scale );

    return 0;
}

void middle_ram_t::stop(void)
{
    int i, j;
    cache.reset(NULL);

    release_ways();
    release_relations();
}

void middle_ram_t::commit(void) {
}

middle_ram_t::middle_ram_t():
    ways(), rels(), way_blocks(0), way_out_count(0), rel_out_count(0), cache(),
    simulate_ways_deleted(false)
{
    ways.resize(NUM_BLOCKS); memset(&ways[0], 0, NUM_BLOCKS * sizeof ways[0]);
    rels.resize(NUM_BLOCKS); memset(&rels[0], 0, NUM_BLOCKS * sizeof rels[0]);
}

middle_ram_t::~middle_ram_t() {
    //instance.reset();
}

std::vector<osmid_t> middle_ram_t::relations_using_way(osmid_t way_id) const
{
    // this function shouldn't be called - relations_using_way is only used in
    // slim mode, and a middle_ram_t shouldn't be constructed if the slim mode
    // option is set.
    throw std::runtime_error("middle_ram_t::relations_using_way is unimlpemented, and "
                             "should not have been called. This is probably a bug, please "
                             "report it at https://github.com/openstreetmap/osm2pgsql/issues");
}

namespace {

void no_delete(const middle_ram_t * middle) {
    // boost::shared_ptr thinks we are going to delete
    // the middle object, but we are not. Heh heh heh.
    // So yeah, this is a hack...
}

}

boost::shared_ptr<const middle_query_t> middle_ram_t::get_instance() const {
    //shallow copy here because readonly access is thread safe
    return boost::shared_ptr<const middle_query_t>(this, no_delete);
}
