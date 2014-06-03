#ifndef TABLE_H
#define TABLE_H

#include "keyvals.hpp"
#include "pgsql.hpp"
#include "osmtypes.hpp"

#include <string>
#include <vector>
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>

typedef std::vector<std::string> hstores_t;
typedef std::vector<std::pair<std::string, std::string> > columns_t;

class table_t : public boost::noncopyable
{
    public:
        table_t(const std::string& name, const std::string& type, const columns_t& columns, const hstores_t& hstore_columns, const int srid,
                const int scale, const bool append, const bool slim, const bool droptemp, const int hstore_mode, const bool enable_hstore_index,
                const boost::optional<std::string>& table_space, const boost::optional<std::string>& table_space_index);
        ~table_t();

        void setup(const std::string& conninfo);
        void stop();
        void teardown();
        void begin();
        void commit();
        void pgsql_pause_copy();
        void write_wkt(const osmid_t id, struct keyval *tags, const char *wkt, struct buffer &sql);
        void write_node(const osmid_t id, struct keyval *tags, double lat, double lon, struct buffer &sql);
        void delete_row(const osmid_t id);

        //interface from retrieving well known text geometry from the table
        struct wkts
        {
            friend class table_t;
            public:
                virtual ~wkts();
                const char* get_next();
                size_t get_count() const;
                void reset();
            private:
                wkts(PGresult* result);
                PGresult* result;
                size_t count;
                size_t current;
        };
        boost::shared_ptr<wkts> get_wkts(const osmid_t id);

    private:
        void export_tags(struct keyval *tags, struct buffer &sql);
        void copy_to_table(const char *sql);
        void write_hstore(keyval *tags);
        void write_hstore_columns(keyval *tags);

        std::string name;
        std::string type;
        pg_conn *sql_conn;
        unsigned int buflen;
        int copyMode;
        char buffer[1024];
        int srid;
        int scale;
        bool append;
        bool slim;
        bool drop_temp;
        int hstore_mode;
        bool enable_hstore_index;
        columns_t columns;
        hstores_t hstore_columns;
        std::string copystr;
        boost::optional<std::string> table_space;
        boost::optional<std::string> table_space_index;
};

#endif
