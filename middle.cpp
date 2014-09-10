#include "middle.hpp"
#include "middle-pgsql.hpp"
#include "middle-ram.hpp"

middle_t* middle_t::create_middle(const bool slim)
{
     return slim ? (middle_t*)new middle_pgsql_t() : (middle_t*)new middle_ram_t();
}


middle_query_t::~middle_query_t() {
}

middle_t::~middle_t() {
}

slim_middle_t::~slim_middle_t() {
}

middle_t::way_cb_func::~way_cb_func() {
}

middle_t::rel_cb_func::~rel_cb_func() {
}

void middle_t::pending_processor::do_batch(boost::lockfree::queue<pending_job>& queue, boost::atomic_size_t& ids_done, const boost::atomic<bool>& done) {
    pending_job job;

    //if we got something or we arent done putting things in the queue
    while (queue.pop(job) || !done) {
        //TODO: reprocess way/rel

        //finished one
        ++ids_done;
    }

    while (queue.pop(job)) {
        //TODO: reprocess way/rel

        //finished one
        ++ids_done;
    }
}

    //starts up count threads and works on the queue
middle_t::pending_processor::pending_processor(size_t thread_count, size_t job_count) {
    //we are not done adding jobs yet
    done = false;

    //nor have we completed any
    ids_done = 0;

    //reserve space for the jobs
    queue.reserve(job_count);

    //make the threads and star them
    for (size_t i = 0; i != thread_count; ++i) {
        workers.create_thread(
            boost::bind(do_batch, boost::ref(queue), boost::ref(ids_done), boost::cref(done)));
    }
}

//waits for the completion of all outstanding jobs
void middle_t::pending_processor::join() {
    //we are done adding jobs
    done = true;

    //wait for them to really be done
    workers.join_all();
}
