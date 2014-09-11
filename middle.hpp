/* Common middle layer interface */

/* Each middle layer data store must provide methods for 
 * storing and retrieving node and way data.
 */

#ifndef MIDDLE_H
#define MIDDLE_H

#include "osmtypes.hpp"
#include "options.hpp"
#include <vector>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/atomic.hpp>

struct keyval;
struct member;

struct middle_query_t {
    virtual ~middle_query_t();

    virtual int nodes_get_list(struct osmNode *out, const osmid_t *nds, int nd_count) const = 0;

    virtual int ways_get(osmid_t id, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const = 0;

    virtual int ways_get_list(const osmid_t *ids, int way_count, osmid_t *way_ids, struct keyval *tag_ptr, struct osmNode **node_ptr, int *count_ptr) const = 0;

    virtual int relations_get(osmid_t id, struct member **members, int *member_count, struct keyval *tags) const = 0;

    virtual std::vector<osmid_t> relations_using_way(osmid_t way_id) const = 0;
};    

struct middle_t : public middle_query_t {
    static middle_t* create_middle(const bool slim);

    virtual ~middle_t();

    virtual int start(const options_t *out_options_) = 0;
    virtual void stop(void) = 0;
    virtual void cleanup(void) = 0;
    virtual void analyze(void) = 0;
    virtual void end(void) = 0;
    virtual void commit(void) = 0;

    virtual int nodes_set(osmid_t id, double lat, double lon, struct keyval *tags) = 0;
    virtual int ways_set(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags) = 0;
    virtual int relations_set(osmid_t id, struct member *members, int member_count, struct keyval *tags) = 0;

    struct way_cb_func {
        virtual ~way_cb_func();
        virtual int operator()(osmid_t id, int exists) = 0;
        virtual void finish(int exists) = 0;
    };
    struct rel_cb_func {
        virtual ~rel_cb_func();
        virtual int operator()(osmid_t id, int exists) = 0;
        virtual void finish(int exists) = 0;
    };

    //force all middles to return some structure that can be used to read from the middle
    struct threadsafe_middle_reader {
        virtual ~threadsafe_middle_reader() {}
        virtual int get_way(osmid_t id, keyval *tags, osmNode **nodes, int *count) = 0;
        virtual int get_relation(osmid_t id, keyval *tags, member **members, int *count) = 0;
        virtual std::vector<osmid_t> get_relations(osmid_t way_id) = 0;
    };
    virtual threadsafe_middle_reader* get_reader() = 0;

    struct pending_job {
        //id we need to process
        osmid_t id;

        //whether this is a way or a rel
        bool is_way;

        //TODO: add these to the thread run method
        //pointer to a middle (for ram can just be a pointer to the real one, for pgsql need to make new ones and connect them)
        //copy of the backends table so it can write the stuff back to it
        //copy of the backends geometry processor
        //copy of the tag transform so we can add/change/delete tags on ways/rels
        //copy of the tag export list so we know what the ending columns will be




        //TODO: when processing a way we need to synchronize access to the rels
        //pending (or have those ids as output from completing a batch)

        //TODO: when expiring we need to synchronize access to or make our own in memory
        //expiry list to provide as output

        //------------------------------------

        //TODO: batch ids so we can query more than one at once from the middle

        //TODO: worry about dealloc of tags and nodes and members
        //and what happens when copying this stuff (push and pop on queue)
        //since multiple jobs will consume the same copy of these things we
        //kind of have to do it outside of the queue or make deep copies of them
    };

    struct pending_processor {

        static void do_batch(boost::lockfree::queue<pending_job>& queue, boost::atomic_size_t& ids_done, const boost::atomic<bool>& done);

        //starts up count threads and works on the queue
        pending_processor(size_t thread_count, size_t job_count);

        //waits for the completion of all outstanding jobs
        void join();

        //actual threads
        boost::thread_group workers;
        //job queue
        boost::lockfree::queue<pending_job> queue;
        //whether or not we are done putting jobs in the queue
        boost::atomic<bool> done;
        //how many ids within the job have been processed
        boost::atomic_size_t ids_done;
    };

    virtual void iterate_ways(way_cb_func &cb) = 0;
    virtual void iterate_relations(rel_cb_func &cb) = 0;

    const options_t* out_options;
};

struct slim_middle_t : public middle_t {
    virtual ~slim_middle_t();

    virtual int nodes_delete(osmid_t id) = 0;
    virtual int node_changed(osmid_t id) = 0;

    virtual int ways_delete(osmid_t id) = 0;
    virtual int way_changed(osmid_t id) = 0;

    virtual int relations_delete(osmid_t id) = 0;
    virtual int relation_changed(osmid_t id) = 0;
};

#endif
