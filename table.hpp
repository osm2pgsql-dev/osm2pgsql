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
                const int scale, const bool append, const bool slim, const bool droptemp, const int enable_hstore,
                const boost::optional<std::string>& table_space, const boost::optional<std::string>& table_space_index);
        ~table_t();

        void setup(const std::string& conninfo);
        void teardown();
        void begin();
        void commit();
        void copy_to_table(const char *sql);
        void write_hstore(keyval *tags, struct buffer &sql);
        void pgsql_pause_copy();
        void write_hstore_columns(keyval *tags, struct buffer &sql);
        void write_way(const osmid_t id, struct keyval *tags, const char *wkt, struct buffer &sql);
        void write_node(const osmid_t id, struct keyval *tags, double lat, double lon, struct buffer &sql);
        void delete_row(const osmid_t id);


        std::string name;
        std::string type;
        struct pg_conn *sql_conn;
        unsigned int buflen;
        int copyMode;
        char buffer[1024];

    private:
        void export_tags(struct keyval *tags, struct buffer &sql);

        int srid;
        int scale;
        bool append;
        bool slim;
        bool drop_temp;
        int enable_hstore;
        columns_t columns;
        hstores_t hstore_columns;
        std::string copystr;
        boost::optional<std::string> table_space;
        boost::optional<std::string> table_space_index;
};

#endif
