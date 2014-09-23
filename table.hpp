#ifndef TABLE_H
#define TABLE_H

#include "keyvals.hpp"
#include "pgsql.hpp"
#include "osmtypes.hpp"

#include <string>
#include <vector>

#include <boost/optional.hpp>
#include <boost/format.hpp>

typedef std::vector<std::string> hstores_t;
typedef std::vector<std::pair<std::string, std::string> > columns_t;
typedef boost::format fmt;

class table_t
{
    public:
        table_t(const std::string& conninfo, const std::string& name, const std::string& type, const columns_t& columns, const hstores_t& hstore_columns, const int srid,
                const int scale, const bool append, const bool slim, const bool droptemp, const int hstore_mode, const bool enable_hstore_index,
                const boost::optional<std::string>& table_space, const boost::optional<std::string>& table_space_index);
        table_t(const table_t& other);
        ~table_t();

        void start();
        void stop();

        void begin();
        void commit();

        void write_wkt(const osmid_t id, struct keyval *tags, const char *wkt);
        void write_node(const osmid_t id, struct keyval *tags, double lat, double lon);
        void delete_row(const osmid_t id);

        std::string const& get_name();

        //interface from retrieving well known text geometry from the table
        struct wkt_reader
        {
            friend class table_t;
            public:
                virtual ~wkt_reader();
                const char* get_next();
                size_t get_count() const;
                void reset();
            private:
                wkt_reader(PGresult* result);
                PGresult* result;
                size_t count;
                size_t current;
        };
        boost::shared_ptr<wkt_reader> get_wkt_reader(const osmid_t id);

    protected:
        void connect();
        void stop_copy();
        void teardown();

        void write_columns(struct keyval *tags, std::string& values);
        void write_tags_column(keyval *tags, std::string& values);
        void write_hstore_columns(keyval *tags, std::string& values);

        void escape4hstore(const char *src, std::string& dst);
        void escape_type(const char *value, const char *type, std::string& dst);

        std::string conninfo;
        std::string name;
        std::string type;
        pg_conn *sql_conn;
        bool copyMode;
        std::string buffer;
        std::string srid;
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

        fmt single_fmt, point_fmt, del_fmt;
};

#endif
