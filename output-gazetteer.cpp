#include <libpq-fe.h>
#include <boost/format.hpp>

#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-gazetteer.hpp"
#include "pgsql.hpp"
#include "util.hpp"
#include "wkb.hpp"

#include <cstring>
#include <iostream>
#include <memory>

void output_gazetteer_t::stop_copy(void)
{
    /* Do we have a copy active? */
    if (!copy_active) return;

    if (buffer.length() > 0)
    {
        pgsql_CopyData("place", Connection, buffer);
        buffer.clear();
    }

    /* Terminate the copy */
    if (PQputCopyEnd(Connection, nullptr) != 1)
    {
        std::cerr << "COPY_END for place failed: " << PQerrorMessage(Connection) << "\n";
        util::exit_nicely();
    }

    /* Check the result */
    pg_result_t res(PQgetResult(Connection));
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        std::cerr << "COPY_END for place failed: " << PQerrorMessage(Connection) << "\n";
        util::exit_nicely();
    }

    /* We no longer have an active copy */
    copy_active = false;
}



void output_gazetteer_t::delete_unused_classes(char osm_type, osmid_t osm_id) {
    char tmp2[2];
    tmp2[0] = osm_type; tmp2[1] = '\0';
    char const *paramValues[2];
    paramValues[0] = tmp2;
    paramValues[1] = (single_fmt % osm_id).str().c_str();
    auto res = pgsql_execPrepared(ConnectionDelete, "get_classes", 2,
                                  paramValues, PGRES_TUPLES_OK);

    int sz = PQntuples(res.get());
    if (sz > 0 && !m_style.has_data()) {
        /* unconditional delete of all places */
        delete_place(osm_type, osm_id);
    } else {
        std::string clslist;
        for (int i = 0; i < sz; i++) {
            std::string cls(PQgetvalue(res.get(), i, 0));
            if (!m_style.has_place(cls)) {
                clslist.reserve(clslist.length() + cls.length() + 3);
                if (!clslist.empty())
                    clslist += ',';
                clslist += '\'';
                clslist += cls;
                clslist += '\'';
            }
        }


        if (!clslist.empty()) {
           /* Stop any active copy */
           stop_copy();

           /* Delete all places for this object */
           pgsql_exec(Connection, PGRES_COMMAND_OK,
                      "DELETE FROM place WHERE osm_type = '%c' AND osm_id = %"
                       PRIdOSMID " and class = any(ARRAY[%s])",
                      osm_type, osm_id, clslist.c_str());
        }
    }
}


void output_gazetteer_t::delete_place(char osm_type, osmid_t osm_id)
{
   /* Stop any active copy */
   stop_copy();

   /* Delete all places for this object */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "DELETE FROM place WHERE osm_type = '%c' AND osm_id = %" PRIdOSMID, osm_type, osm_id);

   return;
}

int output_gazetteer_t::connect() {
    /* Connection to the database */
    Connection = PQconnectdb(m_options.database_options.conninfo().c_str());

    /* Check to see that the backend connection was successfully made */
    if (PQstatus(Connection) != CONNECTION_OK) {
       std::cerr << "Connection to database failed: " << PQerrorMessage(Connection) << "\n";
       return 1;
    }

    if (m_options.append) {
        ConnectionDelete = PQconnectdb(m_options.database_options.conninfo().c_str());
        if (PQstatus(ConnectionDelete) != CONNECTION_OK)
        {
            std::cerr << "Connection to database failed: " << PQerrorMessage(ConnectionDelete) << "\n";
            return 1;
        }

        pgsql_exec(ConnectionDelete, PGRES_COMMAND_OK, "PREPARE get_classes (CHAR(1), " POSTGRES_OSMID_TYPE ") AS SELECT class FROM place WHERE osm_type = $1 and osm_id = $2");
    }
    return 0;
}

int output_gazetteer_t::start()
{
    int srid = m_options.projection->target_srs();

    if (connect()) {
        util::exit_nicely();
    }

    /* Start a transaction */
    pgsql_exec(Connection, PGRES_COMMAND_OK, "BEGIN");

    /* (Re)create the table unless we are appending */
    if (!m_options.append) {
        /* Drop any existing table */
        pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS place CASCADE");

        /* Create the new table */

        std::string sql =
            "CREATE TABLE place ("
            "  osm_id " POSTGRES_OSMID_TYPE " NOT NULL,"
            "  osm_type char(1) NOT NULL,"
            "  class TEXT NOT NULL,"
            "  type TEXT NOT NULL,"
            "  name HSTORE,"
            "  admin_level SMALLINT,"
            "  address HSTORE,"
            "  extratags HSTORE," +
            (boost::format("  geometry Geometry(Geometry,%1%) NOT NULL") % srid)
                .str() +
            ")";
        if (m_options.tblsmain_data) {
            sql += " TABLESPACE " + m_options.tblsmain_data.get();
        }

        pgsql_exec_simple(Connection, PGRES_COMMAND_OK, sql);

        std::string index_sql =
            "CREATE INDEX place_id_idx ON place USING BTREE (osm_type, osm_id)";
        if (m_options.tblsmain_index) {
            index_sql += " TABLESPACE " + m_options.tblsmain_index.get();
        }
        pgsql_exec_simple(Connection, PGRES_COMMAND_OK, index_sql);
    }

    return 0;
}

void output_gazetteer_t::stop(osmium::thread::Pool *)
{
   /* Stop any active copy */
   stop_copy();

   /* Commit transaction */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "COMMIT");


   PQfinish(Connection);
   if (ConnectionDelete)
       PQfinish(ConnectionDelete);
   if (ConnectionError)
       PQfinish(ConnectionError);

   return;
}

int output_gazetteer_t::process_node(osmium::Node const &node)
{
    m_style.process_tags(node);

    if (m_options.append)
        delete_unused_classes('N', node.id());

    /* Are we interested in this item? */
    if (m_style.has_data()) {
        auto wkb = m_builder.get_wkb_node(node.location());
        m_style.copy_out(node, wkb, buffer);
        flush_place_buffer();
    }

    return 0;
}

int output_gazetteer_t::process_way(osmium::Way *way)
{
    m_style.process_tags(*way);

    if (m_options.append)
        delete_unused_classes('W', way->id());

    /* Are we interested in this item? */
    if (m_style.has_data()) {
        /* Fetch the node details */
        m_mid->nodes_get_list(&(way->nodes()));

        /* Get the geometry of the object */
        geom::osmium_builder_t::wkb_t geom;
        if (way->is_closed()) {
            geom = m_builder.get_wkb_polygon(*way);
        }
        if (geom.empty()) {
            auto wkbs = m_builder.get_wkb_line(way->nodes(), 0.0);
            if (wkbs.empty()) {
                return 0;
            }

            geom = wkbs[0];
        }

        m_style.copy_out(*way, geom, buffer);
        flush_place_buffer();
    }

    return 0;
}

int output_gazetteer_t::process_relation(osmium::Relation const &rel)
{
    auto const &tags = rel.tags();
    char const *type = tags["type"];
    if (!type) {
        delete_unused_full('R', rel.id());
        return 0;
    }

    bool is_waterway = strcmp(type, "waterway") == 0;

    if (strcmp(type, "associatedStreet") == 0
        || !(strcmp(type, "boundary") == 0
             || strcmp(type, "multipolygon") == 0 || is_waterway)) {
        delete_unused_full('R', rel.id());
        return 0;
    }

    m_style.process_tags(rel);

    if (m_options.append)
        delete_unused_classes('R', rel.id());

    /* Are we interested in this item? */
    if (!m_style.has_data())
        return 0;

    /* get the boundary path (ways) */
    osmium_buffer.clear();
    auto num_ways = m_mid->rel_way_members_get(rel, nullptr, osmium_buffer);

    if (num_ways == 0) {
        delete_unused_full('R', rel.id());

        return 0;
    }

    for (auto &w : osmium_buffer.select<osmium::Way>()) {
        m_mid->nodes_get_list(&(w.nodes()));
    }

    auto geoms = is_waterway
                     ? m_builder.get_wkb_multiline(osmium_buffer, 0.0)
                     : m_builder.get_wkb_multipolygon(rel, osmium_buffer);

    if (!geoms.empty()) {
        m_style.copy_out(rel, geoms[0], buffer);
        flush_place_buffer();
    }

    return 0;
}
