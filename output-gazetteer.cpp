#include <libpq-fe.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include "middle.hpp"
#include "options.hpp"
#include "osmtypes.hpp"
#include "output-gazetteer.hpp"
#include "pgsql.hpp"
#include "util.hpp"
#include "wkb.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>

enum : int { MAX_ADMINLEVEL = 15 };

void place_tag_processor::clear()
{
    // set members to sane defaults
    admin_level = MAX_ADMINLEVEL;

    places.clear();
    extratags.clear();
    address.clear();
    names.clear();
}

struct UnnamedPredicate
{
    bool operator()(const tag_t &val) const {
        return val.key == "natural" ||
               val.key == "railway" ||
               val.key == "waterway" ||
               val.key == "boundary" ||
               (val.key == "highway" &&
                (val.value == "traffic_signals" ||
                 val.value == "service" ||
                 val.value == "cycleway" ||
                 val.value == "path" ||
                 val.value == "footway" ||
                 val.value == "steps" ||
                 val.value == "bridleway" ||
                 val.value == "track" ||
                 val.value == "byway" ||
                 boost::ends_with(val.value, "_link")));
    }
};

void place_tag_processor::process_tags(osmium::OSMObject const &o)
{
    bool placeadmin = false;
    bool placehouse = false;
    bool placebuilding = false;
    osmium::Tag const *place = 0;
    osmium::Tag const *junction = 0;
    osmium::Tag const *landuse = 0;
    bool isnamed = false;
    bool isinterpolation = false;

    clear();

    for (const auto &item: o.tags()) {
        char const *k = item.key();
        char const *v = item.value();
        if (boost::ends_with(k, "source")) {
            // ignore
        } else if (strcmp(k, "name:prefix") == 0 ||
                   strcmp(k, "name:botanical") == 0 ||
                   boost::ends_with(k, "wikidata")) {
            extratags.push_back(&item);
        } else if (strcmp(k, "ref") == 0 ||
                   strcmp(k, "int_ref") == 0 ||
                   strcmp(k, "nat_ref") == 0 ||
                   strcmp(k, "reg_ref") == 0 ||
                   strcmp(k, "loc_ref") == 0 ||
                   strcmp(k, "old_ref") == 0 ||
                   strcmp(k, "iata") == 0 ||
                   strcmp(k, "icao") == 0 ||
                   strcmp(k, "operator") == 0 ||
                   strcmp(k, "pcode") == 0 ||
                   boost::starts_with(k, "pcode:")) {
            names.push_back(&item);
        } else if (strcmp(k, "name") == 0 ||
                   boost::starts_with(k, "name:") ||
                   strcmp(k, "int_name") == 0 ||
                   boost::starts_with(k, "int_name:") ||
                   strcmp(k, "nat_name") == 0 ||
                   boost::starts_with(k, "nat_name:") ||
                   strcmp(k, "reg_name") == 0 ||
                   boost::starts_with(k, "reg_name:") ||
                   strcmp(k, "loc_name") == 0 ||
                   boost::starts_with(k, "loc_name:") ||
                   strcmp(k, "old_name") == 0 ||
                   boost::starts_with(k, "old_name:") ||
                   strcmp(k, "alt_name") == 0 ||
                   boost::starts_with(k, "alt_name:") ||
                   boost::starts_with(k, "alt_name_") ||
                   strcmp(k, "official_name") == 0 ||
                   boost::starts_with(k, "official_name:") ||
                   strcmp(k, "place_name") == 0 ||
                   boost::starts_with(k, "place_name:") ||
                   strcmp(k, "short_name") == 0 ||
                   boost::starts_with(k, "short_name:") ||
                   strcmp(k, "brand") == 0) {
            names.push_back(&item);
            isnamed = true;
        } else if (strcmp(k, "addr:housename") == 0) {
            names.push_back(&item);
            placehouse = true;
        } else if (strcmp(k, "emergency") == 0) {
            if (strcmp(v, "fire_hydrant") != 0 &&
                strcmp(v, "yes") != 0 &&
                strcmp(v, "no") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "tourism") == 0 ||
                   strcmp(k, "historic") == 0 ||
                   strcmp(k, "military") == 0) {
            if (strcmp(v, "no") != 0 && strcmp(v, "yes") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "natural") == 0) {
            if (strcmp(v, "no") != 0 &&
                strcmp(v, "yes") != 0 &&
                strcmp(v, "coastline") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "landuse") == 0) {
            if (strcmp(v, "cemetry") == 0)
                places.emplace_back(k, v);
            else
                landuse = &item;
        } else if (strcmp(k, "highway") == 0) {
            if (strcmp(v, "footway") == 0) {
                auto *footway = o.tags()["footway"];
                if (footway == nullptr || strcmp(footway, "sidewalk") != 0)
                    places.emplace_back(k, v);
            } else if (strcmp(v, "no") != 0 &&
                strcmp(v, "turning_circle") != 0 &&
                strcmp(v, "mini_roundabout") != 0 &&
                strcmp(v, "noexit") != 0 &&
                strcmp(v, "crossing") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "railway") == 0) {
            if (strcmp(v, "level_crossing") != 0 &&
                strcmp(v, "no") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "man_made") == 0) {
            if (strcmp(v, "survey_point") != 0 &&
                strcmp(v, "cutline") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "aerialway") == 0) {
            if (strcmp(v, "pylon") != 0 &&
                strcmp(v, "no") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "boundary") == 0) {
            if (strcmp(v, "administrative") == 0)
                placeadmin = true;
            places.emplace_back(k, v);
        } else if (strcmp(k, "aeroway") == 0 ||
                   strcmp(k, "amenity") == 0 ||
                   strcmp(k, "club") == 0 ||
                   strcmp(k, "boundary") == 0 ||
                   strcmp(k, "bridge") == 0 ||
                   strcmp(k, "craft") == 0 ||
                   strcmp(k, "leisure") == 0 ||
                   strcmp(k, "office") == 0 ||
                   strcmp(k, "shop") == 0 ||
                   strcmp(k, "tunnel") == 0 ||
                   strcmp(k, "mountain_pass") == 0) {
            if (strcmp(v, "no") != 0)
            {
                places.emplace_back(k, v);
            }
        } else if (strcmp(k, "waterway") == 0) {
            if (strcmp(v, "riverbank") != 0)
                places.emplace_back(k, v);
        } else if (strcmp(k, "place") == 0) {
            place = &item;
        } else if (strcmp(k, "junction") == 0) {
            junction = &item;
        } else if (strcmp(k, "postal_code") == 0 ||
                   strcmp(k, "postcode") == 0 ||
                   strcmp(k, "addr:postcode") == 0 ||
                   strcmp(k, "tiger:zip_left") == 0 ||
                   strcmp(k, "tiger:zip_right") == 0) {
            if (address.find("postcode") == address.end()) {
                address.emplace("postcode", v);
            }
        } else if (strcmp(k, "country_code") == 0 ||
                   strcmp(k, "ISO3166-1") == 0 ||
                   strcmp(k, "is_in:country_code") == 0 ||
                   strcmp(k, "is_in:country") == 0 ||
                   strcmp(k, "addr:country") == 0 ||
                   strcmp(k, "addr:country_code") == 0) {
            if (strlen(v) == 2 && address.find("country") == address.end()) {
                address.emplace("country", v);
            }
        } else if (boost::starts_with(k, "addr:")) {
            if (strcmp(k, "addr:interpolation") == 0) {
                isinterpolation = true;
            }
            if (strcmp(k, "addr:housenumber") == 0 ||
                strcmp(k, "addr:conscriptionnumber") == 0 ||
                strcmp(k, "addr:streetnumber") == 0) {
                placehouse = true;
            }
            address.emplace(k + 5, v);
        } else if (boost::starts_with(k, "is_in:")) {
            if (address.find(k + 6) == address.end()) {
                address.emplace(k + 6, v);
            }
        } else if (strcmp(k, "is_in") == 0 ||
                   strcmp(k, "tiger:county") == 0) {
            address.emplace(k, v);
        } else if (strcmp(k, "admin_level") == 0) {
            admin_level = atoi(v);
            if (admin_level <= 0 || admin_level > MAX_ADMINLEVEL)
                admin_level = MAX_ADMINLEVEL;
        } else if (strcmp(k, "tracktype") == 0 ||
                   strcmp(k, "traffic_calming") == 0 ||
                   strcmp(k, "service") == 0 ||
                   strcmp(k, "cuisine") == 0 ||
                   strcmp(k, "capital") == 0 ||
                   strcmp(k, "dispensing") == 0 ||
                   strcmp(k, "religion") == 0 ||
                   strcmp(k, "denomination") == 0 ||
                   strcmp(k, "sport") == 0 ||
                   strcmp(k, "internet_access") == 0 ||
                   strcmp(k, "lanes") == 0 ||
                   strcmp(k, "surface") == 0 ||
                   strcmp(k, "smoothness") == 0 ||
                   strcmp(k, "width") == 0 ||
                   strcmp(k, "est_width") == 0 ||
                   strcmp(k, "incline") == 0 ||
                   strcmp(k, "opening_hours") == 0 ||
                   strcmp(k, "collection_times") == 0 ||
                   strcmp(k, "service_times") == 0 ||
                   strcmp(k, "disused") == 0 ||
                   strcmp(k, "wheelchair") == 0 ||
                   strcmp(k, "sac_scale") == 0 ||
                   strcmp(k, "trail_visibility") == 0 ||
                   strcmp(k, "mtb:scale") == 0 ||
                   strcmp(k, "mtb:description") == 0 ||
                   strcmp(k, "wood") == 0 ||
                   strcmp(k, "drive_through") == 0 ||
                   strcmp(k, "drive_in") == 0 ||
                   strcmp(k, "access") == 0 ||
                   strcmp(k, "vehicle") == 0 ||
                   strcmp(k, "bicyle") == 0 ||
                   strcmp(k, "foot") == 0 ||
                   strcmp(k, "goods") == 0 ||
                   strcmp(k, "hgv") == 0 ||
                   strcmp(k, "motor_vehicle") == 0 ||
                   strcmp(k, "motor_car") == 0 ||
                   boost::starts_with(k, "access:") ||
                   boost::starts_with(k, "contact:") ||
                   boost::starts_with(k, "drink:") ||
                   strcmp(k, "oneway") == 0 ||
                   strcmp(k, "date_on") == 0 ||
                   strcmp(k, "date_off") == 0 ||
                   strcmp(k, "day_on") == 0 ||
                   strcmp(k, "day_off") == 0 ||
                   strcmp(k, "hour_on") == 0 ||
                   strcmp(k, "hour_off") == 0 ||
                   strcmp(k, "maxweight") == 0 ||
                   strcmp(k, "maxheight") == 0 ||
                   strcmp(k, "maxspeed") == 0 ||
                   strcmp(k, "fee") == 0 ||
                   strcmp(k, "toll") == 0 ||
                   boost::starts_with(k, "toll:") ||
                   strcmp(k, "charge") == 0 ||
                   strcmp(k, "population") == 0 ||
                   strcmp(k, "description") == 0 ||
                   strcmp(k, "image") == 0 ||
                   strcmp(k, "attribution") == 0 ||
                   strcmp(k, "fax") == 0 ||
                   strcmp(k, "email") == 0 ||
                   strcmp(k, "url") == 0 ||
                   strcmp(k, "website") == 0 ||
                   strcmp(k, "phone") == 0 ||
                   strcmp(k, "real_ale") == 0 ||
                   strcmp(k, "smoking") == 0 ||
                   strcmp(k, "food") == 0 ||
                   strcmp(k, "camera") == 0 ||
                   strcmp(k, "brewery") == 0 ||
                   strcmp(k, "locality") == 0 ||
                   strcmp(k, "wikipedia") == 0 ||
                   boost::starts_with(k, "wikipedia:")) {
            extratags.push_back(&item);
        } else if (strcmp(k, "building") == 0) {
            placebuilding = true;
        }
    }

    // skip some tags, if they don't have a proper name (ref doesn't count)
    if (!isnamed) {
        if (!places.empty())
            places.erase(std::remove_if(places.begin(), places.end(),
                                        UnnamedPredicate()),
                         places.end());
    }

    if (isinterpolation)
        places.emplace_back("place", "houses");

    if (place) {
        if (isinterpolation ||
             (placeadmin &&
              strcmp(place->value(), "island") != 0 &&
              strcmp(place->value(), "islet") != 0))
            extratags.emplace_back(place);
        else
            places.emplace_back(place->key(), place->value());
    }

    if (isnamed && places.empty()) {
        if (junction)
            places.emplace_back(junction->key(), junction->value());
        else if (landuse)
            places.emplace_back(landuse->key(), landuse->value());
    }

    if (places.empty()) {
        bool postcode = address.find("postcode") != address.end();
        if (placebuilding && (!names.empty() || placehouse || postcode)) {
            places.emplace_back("building", "yes");
        } else if (placehouse) {
            places.emplace_back("place", "house");
        } else if (postcode) {
            places.emplace_back("place", "postcode");
        }
    }
}

void place_tag_processor::copy_out(osmium::OSMObject const &o,
                                   const std::string &geom,
                                   std::string &buffer)
{
    for (const auto& place: places) {
        std::string name;
        if (place.key == "bridge" || place.key == "tunnel") {
            name = domain_name(place.key, o.tags());
            if (name.empty())
                continue; // don't include unnamed bridges and tunnels
        }

        // osm_type
        buffer += (char) toupper(osmium::item_type_to_char(o.type()));
        buffer += '\t';
        // osm_id
        buffer += (single_fmt % o.id()).str();
        // class
        escape(place.key, buffer);
        buffer += '\t';
        // type
        escape(place.value, buffer);
        buffer += '\t';
        // names
        if (!name.empty()) {
            buffer += name;
            buffer += '\t';
        } else if (!names.empty()) {
            bool first = true;
            // operator will be ignored on anything but these classes
            // (amenity for restaurant and fuel)
            bool shop = (place.key == "shop") ||
                        (place.key == "amenity") ||
                        (place.key == "tourism");
            for (const auto entry: names) {
                if (!shop && strcmp(entry->key(), "operator") == 0)
                    continue;

                if (first) {
                    first = false;
                } else {
                    buffer += ',';
                }

                buffer += "\"";
                escape_array_record(entry->key(), buffer);
                buffer += "\"=>\"";
                escape_array_record(entry->value(), buffer);
                buffer += "\"";
            }

            buffer += first ? "\\N\t" : "\t";
        } else
            buffer += "\\N\t";
        // admin_level
        buffer += (single_fmt % admin_level).str();
        // address
        if (address.empty()) {
            buffer += "\\N\t";
        } else {
            for (auto const &a : address) {
                buffer += "\"";
                escape_array_record(a.first, buffer);
                buffer += "\"=>\"";
                if (a.first == "tiger:county") {
                    auto *end = strchr(a.second, ',');
                    if (end) {
                        size_t len = (size_t) (end - a.second);
                        escape_array_record(std::string(a.second, len), buffer);
                    } else {
                        escape_array_record(a.second, buffer);
                    }
                    buffer += " county";
                } else {
                    escape_array_record(a.second, buffer);
                }
                buffer += "\",";
            }
            buffer[buffer.length() - 1] = '\t';
        } 
        // extra tags
        if (extratags.empty()) {
            buffer += "\\N\t";
        } else {
            for (const auto entry: extratags) {
                buffer += "\"";
                escape_array_record(entry->key(), buffer);
                buffer += "\"=>\"";
                escape_array_record(entry->value(), buffer);
                buffer += "\",";
            }
            buffer[buffer.length() - 1] = '\t';
        }
        // add the geometry - encoding it to hex along the way
        ewkb::writer_t::write_as_hex(buffer, geom);
        buffer += '\n';
    }
}


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
    if (sz > 0 && !places.has_data()) {
        /* unconditional delete of all places */
        delete_place(osm_type, osm_id);
    } else {
        std::string clslist;
        for (int i = 0; i < sz; i++) {
            std::string cls(PQgetvalue(res.get(), i, 0));
            if (!places.has_place(cls)) {
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
    places.process_tags(node);

    if (m_options.append)
        delete_unused_classes('N', node.id());

    /* Are we interested in this item? */
    if (places.has_data()) {
        auto wkb = m_builder.get_wkb_node(node.location());
        places.copy_out(node, wkb, buffer);
        flush_place_buffer();
    }

    return 0;
}

int output_gazetteer_t::process_way(osmium::Way *way)
{
    places.process_tags(*way);

    if (m_options.append)
        delete_unused_classes('W', way->id());

    /* Are we interested in this item? */
    if (places.has_data()) {
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

        places.copy_out(*way, geom, buffer);
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

    places.process_tags(rel);

    if (m_options.append)
        delete_unused_classes('R', rel.id());

    /* Are we interested in this item? */
    if (!places.has_data())
        return 0;

    /* get the boundary path (ways) */
    osmium_buffer.clear();
    auto num_ways = m_mid->rel_way_members_get(rel, nullptr, osmium_buffer);

    if (num_ways == 0) {
        if (m_options.append)
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
        places.copy_out(rel, geoms[0], buffer);
        flush_place_buffer();
    }

    return 0;
}
