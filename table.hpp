#ifndef TABLE_H
#define TABLE_H

#include "keyvals.hpp"
#include "pgsql.hpp"

#include <string>
#include <vector>
#include <boost/noncopyable.hpp>

class table_t : public boost::noncopyable
{
    public:
        table_t(const char *name_, const char *type_, const int srs, const int enable_hstore, const std::vector<std::string>& hstore_columns);
        ~table_t();
        char *name;
        const char *type;
        struct pg_conn *sql_conn;
        unsigned int buflen;
        int copyMode;
        char *columns;
        char buffer[1024];
    
        void connect(const char* conninfo);
        void disconnect();
        void commit();
        void copy_to_table(const char *sql);
        void write_hstore(keyval *tags, struct buffer &sql);
        void pgsql_pause_copy();
        void write_hstore_columns(keyval *tags, struct buffer &sql);
    
    private:
        int srs;
        int enable_hstore;
        std::vector<std::string> hstore_columns;
};

#endif
