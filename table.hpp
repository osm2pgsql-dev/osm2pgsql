#ifndef TABLE_H
#define TABLE_H

#include "pgsql.hpp"
#include "osmtypes.hpp"

#include <cstddef>
#include <string>
#include <vector>
#include <utility>

#include <boost/optional.hpp>
#include <boost/format.hpp>
#include <boost/shared_ptr.hpp>

typedef std::vector<std::string> hstores_t;
typedef std::vector<std::pair<std::string, std::string> > columns_t;

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

        void write_wkt(const osmid_t id, const taglist_t &tags, const char *wkt);
        void write_node(const osmid_t id, const taglist_t &tags, double lat, double lon);
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

        void write_columns(const taglist_t &tags, std::string& values, std::vector<bool> *used);
        void write_tags_column(const taglist_t &tags, std::string& values,
                               const std::vector<bool> &used);
        void write_hstore_columns(const taglist_t &tags, std::string& values);

        void escape4hstore(const char *src, std::string& dst);
        void escape_type(const std::string &value, const std::string &type, std::string& dst);

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

        boost::format single_fmt, point_fmt, del_fmt;
};

#endif
