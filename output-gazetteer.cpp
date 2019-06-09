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

void output_gazetteer_t::delete_unused_classes(char const *osm_type,
                                               osmid_t osm_id)
{
    if (!m_style.has_data()) {
        /* unconditional delete of all places */
        delete_place(osm_type, osm_id);
        return;
    }

    char id[64];
    snprintf(id, sizeof(id), "%" PRIdOSMID, osm_id);
    char const *params[2] = {osm_type, id};

    auto res = pgsql_execPrepared(m_conn, "get_classes", 2, params,
                                  PGRES_TUPLES_OK);
    int sz = PQntuples(res.get());

    std::string clslist;
    for (int i = 0; i < sz; i++) {
        std::string cls(PQgetvalue(res.get(), i, 0));
        if (!m_style.has_place(cls)) {
            clslist += '\'';
            clslist += cls;
            clslist += "\',";
        }
    }

    if (!clslist.empty()) {
        clslist[clslist.size() - 1] = '\0';
        // Delete places where classes have disappeared for this object.
        pgsql_exec(m_conn, PGRES_COMMAND_OK,
                "DELETE FROM place WHERE osm_type = '%s' AND osm_id = %"
                PRIdOSMID " and class = any(ARRAY[%s])",
                osm_type, osm_id, clslist.c_str());
    }
}

void output_gazetteer_t::delete_place(char const *osm_type, osmid_t osm_id)
{
    char id[64];
    snprintf(id, sizeof(id), "%" PRIdOSMID, osm_id);
    char const *params[2] = {osm_type, id};

    pgsql_execPrepared(m_conn, "delete_place", 2, params, PGRES_COMMAND_OK);
}

void output_gazetteer_t::connect()
{
    m_conn = PQconnectdb(m_options.database_options.conninfo().c_str());

    /* Check to see that the backend connection was successfully made */
    if (PQstatus(m_conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n",
                PQerrorMessage(m_conn));
        throw std::runtime_error("Connecting to database");
    }
}

int output_gazetteer_t::start()
{
    int srid = m_options.projection->target_srs();

    connect();

    /* (Re)create the table unless we are appending */
    if (!m_options.append) {
        /* Drop any existing table */
        pgsql_exec(m_conn, PGRES_COMMAND_OK,
                   "DROP TABLE IF EXISTS place CASCADE");

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

        pgsql_exec_simple(m_conn, PGRES_COMMAND_OK, sql);

        std::string index_sql =
            "CREATE INDEX place_id_idx ON place USING BTREE (osm_type, osm_id)";
        if (m_options.tblsmain_index) {
            index_sql += " TABLESPACE " + m_options.tblsmain_index.get();
        }
        pgsql_exec_simple(m_conn, PGRES_COMMAND_OK, index_sql);
    }

    prepare_query_conn();

    return 0;
}

void output_gazetteer_t::prepare_query_conn() const
{
    pgsql_exec(m_conn, PGRES_COMMAND_OK,
           "PREPARE get_classes (CHAR(1), " POSTGRES_OSMID_TYPE ") "
           "AS SELECT class FROM place "
           "WHERE osm_type = $1 and osm_id = $2");
    pgsql_exec(m_conn, PGRES_COMMAND_OK,
           "PREPARE delete_place (CHAR(1), " POSTGRES_OSMID_TYPE ") "
           "AS DELETE FROM place WHERE osm_type = $1 and osm_id = $2");
}

output_gazetteer_t::~output_gazetteer_t()
{
    if (m_conn)
        PQfinish(m_conn);
}

void output_gazetteer_t::commit() { m_copy.sync(); }

int output_gazetteer_t::process_node(osmium::Node const &node)
{
    m_style.process_tags(node);

    if (m_options.append)
        delete_unused_classes("N", node.id());

    /* Are we interested in this item? */
    if (m_style.has_data()) {
        auto wkb = m_builder.get_wkb_node(node.location());
        if (!m_style.copy_out(node, wkb, m_copy))
            delete_unused_full("N", node.id());
    }

    return 0;
}

int output_gazetteer_t::process_way(osmium::Way *way)
{
    m_style.process_tags(*way);

    if (m_options.append)
        delete_unused_classes("W", way->id());

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

        if (!m_style.copy_out(*way, geom, m_copy))
            delete_unused_full("W", way->id());
    }

    return 0;
}

int output_gazetteer_t::process_relation(osmium::Relation const &rel)
{
    auto const &tags = rel.tags();
    char const *type = tags["type"];
    if (!type) {
        delete_unused_full("R", rel.id());
        return 0;
    }

    bool is_waterway = strcmp(type, "waterway") == 0;

    if (strcmp(type, "associatedStreet") == 0
        || !(strcmp(type, "boundary") == 0
             || strcmp(type, "multipolygon") == 0 || is_waterway)) {
        delete_unused_full("R", rel.id());
        return 0;
    }

    m_style.process_tags(rel);

    if (m_options.append)
        delete_unused_classes("R", rel.id());

    /* Are we interested in this item? */
    if (!m_style.has_data())
        return 0;

    /* get the boundary path (ways) */
    osmium_buffer.clear();
    auto num_ways = m_mid->rel_way_members_get(rel, nullptr, osmium_buffer);

    if (num_ways == 0) {
        delete_unused_full("R", rel.id());
        return 0;
    }

    for (auto &w : osmium_buffer.select<osmium::Way>()) {
        m_mid->nodes_get_list(&(w.nodes()));
    }

    auto geoms = is_waterway
                     ? m_builder.get_wkb_multiline(osmium_buffer, 0.0)
                     : m_builder.get_wkb_multipolygon(rel, osmium_buffer);

    if (!geoms.empty()) {
        if (!m_style.copy_out(rel, geoms[0], m_copy))
            delete_unused_full("R", rel.id());
    }

    return 0;
}
