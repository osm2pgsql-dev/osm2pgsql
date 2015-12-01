#include <libpq-fe.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include "osmtypes.hpp"
#include "middle.hpp"
#include "pgsql.hpp"
#include "reprojection.hpp"
#include "output-gazetteer.hpp"
#include "options.hpp"
#include "util.hpp"

#include <algorithm>
#include <iostream>
#include <memory>

#define SRID (reproj->project_getprojinfo()->srs)

#define CREATE_PLACE_TABLE                      \
   "CREATE TABLE place ("                       \
   "  osm_type CHAR(1) NOT NULL,"               \
   "  osm_id " POSTGRES_OSMID_TYPE " NOT NULL," \
   "  class TEXT NOT NULL,"                     \
   "  type TEXT NOT NULL,"                      \
   "  name HSTORE,"                             \
   "  admin_level INTEGER,"                     \
   "  housenumber TEXT,"                        \
   "  street TEXT,"                             \
   "  addr_place TEXT,"                         \
   "  isin TEXT,"                               \
   "  postcode TEXT,"                           \
   "  country_code VARCHAR(2),"                 \
   "  extratags HSTORE"                         \
   ") %s %s"

#define ADMINLEVEL_NONE 100

#define CREATE_PLACE_ID_INDEX \
   "CREATE INDEX place_id_idx ON place USING BTREE (osm_type, osm_id) %s %s"


enum { BUFFER_SIZE = 4092 };

void place_tag_processor::clear()
{
    // set members to sane defaults
    src = nullptr;
    admin_level = ADMINLEVEL_NONE;
    countrycode = 0;
    housenumber.assign("\\N");
    street = 0;
    addr_place = 0;
    postcode = 0;

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

void place_tag_processor::process_tags(const taglist_t &tags)
{
    bool placeadmin = false;
    bool placehouse = false;
    bool placebuilding = false;
    const tag_t *place = 0;
    const tag_t *junction = 0;
    const tag_t *landuse = 0;
    bool isnamed = false;
    bool isinterpolation = false;
    const std::string *house_nr = 0;
    const std::string *conscr_nr = 0;
    const std::string *street_nr = 0;

    clear();
    src = &tags;

    for (const auto& item: tags) {
        if (boost::ends_with(item.key, "source")) {
            // ignore
        } else if (item.key == "name:prefix" ||
                   item.key == "name:botanical" ||
                   boost::ends_with(item.key, "wikidata")) {
            extratags.push_back(&item);
        } else if (item.key == "ref" ||
                   item.key == "int_ref" ||
                   item.key == "nat_ref" ||
                   item.key == "reg_ref" ||
                   item.key == "loc_ref" ||
                   item.key == "old_ref" ||
                   item.key == "iata" ||
                   item.key == "icao" ||
                   item.key == "operator" ||
                   item.key == "pcode" ||
                   boost::starts_with(item.key, "pcode:")) {
            names.push_back(&item);
        } else if (item.key == "name" ||
                   boost::starts_with(item.key, "name:") ||
                   item.key == "int_name" ||
                   boost::starts_with(item.key, "int_name:") ||
                   item.key == "nat_name" ||
                   boost::starts_with(item.key, "nat_name:") ||
                   item.key == "reg_name" ||
                   boost::starts_with(item.key, "reg_name:") ||
                   item.key == "loc_name" ||
                   boost::starts_with(item.key, "loc_name:") ||
                   item.key == "old_name" ||
                   boost::starts_with(item.key, "old_name:") ||
                   item.key == "alt_name" ||
                   boost::starts_with(item.key, "alt_name:") ||
                   boost::starts_with(item.key, "alt_name_") ||
                   item.key == "official_name" ||
                   boost::starts_with(item.key, "official_name:") ||
                   item.key == "place_name" ||
                   boost::starts_with(item.key, "place_name:") ||
                   item.key == "short_name" ||
                   boost::starts_with(item.key, "short_name:") ||
                   item.key == "brand") {
            names.push_back(&item);
            isnamed = true;
        } else if (item.key == "addr:housename") {
            names.push_back(&item);
            placehouse = true;
        } else if (item.key == "emergency") {
            if (item.value != "fire_hydrant" &&
                item.value != "yes" &&
                item.value != "no")
                places.push_back(item);
        } else if (item.key == "tourism" ||
                   item.key == "historic" ||
                   item.key == "military") {
            if (item.value != "no" && item.value != "yes")
                places.push_back(item);
        } else if (item.key == "natural") {
            if (item.value != "no" &&
                item.value != "yes" &&
                item.value != "coastline")
                places.push_back(item);
        } else if (item.key == "landuse") {
            if (item.value == "cemetry")
                places.push_back(item);
            else
                landuse = &item;
        } else if (item.key == "highway") {
            if (item.value == "footway") {
                auto *footway = tags.get("footway");
                if (footway == nullptr || *footway != "sidewalk")
                    places.push_back(item);
            } else if (item.value != "no" &&
                item.value != "turning_circle" &&
                item.value != "mini_roundabout" &&
                item.value != "noexit" &&
                item.value != "crossing")
                places.push_back(item);
        } else if (item.key == "railway") {
            if (item.value != "level_crossing" &&
                item.value != "no")
                places.push_back(item);
        } else if (item.key == "man_made") {
            if (item.value != "survey_point" &&
                item.value != "cutline")
                places.push_back(item);
        } else if (item.key == "aerialway") {
            if (item.value != "pylon" &&
                item.value != "no")
                places.push_back(item);
        } else if (item.key == "boundary") {
            if (item.value == "administrative")
                placeadmin = true;
            places.push_back(item);
        } else if (item.key == "aeroway" ||
                   item.key == "amenity" ||
                   item.key == "boundary" ||
                   item.key == "bridge" ||
                   item.key == "craft" ||
                   item.key == "leisure" ||
                   item.key == "office" ||
                   item.key == "shop" ||
                   item.key == "tunnel" ||
                   item.key == "mountain_pass") {
            if (item.value != "no")
            {
                places.push_back(item);
            }
        } else if (item.key == "waterway") {
            if (item.value != "riverbank")
                places.push_back(item);
        } else if (item.key == "place") {
            place = &item;
        } else if (item.key == "junction") {
            junction = &item;
        } else if (item.key == "addr:interpolation") {
            housenumber.clear();
            escape(item.value, housenumber);
            isinterpolation = true;
        } else if (item.key == "addr:housenumber") {
            house_nr = &item.value;
            placehouse = true;
        } else if (item.key == "addr:conscriptionnumber") {
            conscr_nr = &item.value;
            placehouse = true;
        } else if (item.key == "addr:streetnumber") {
            street_nr = &item.value;
            placehouse = true;
        } else if (item.key == "addr:street") {
            street = &item.value;
        } else if (item.key == "addr:place") {
            addr_place = &item.value;
        } else if (item.key == "postal_code" ||
                   item.key == "postcode" ||
                   item.key == "addr:postcode" ||
                   item.key == "tiger:zip_left" ||
                   item.key == "tiger:zip_right") {
            if (!postcode)
                postcode = &item.value;
        } else if (item.key == "country_code" ||
                   item.key == "ISO3166-1" ||
                   item.key == "is_in:country_code" ||
                   item.key == "addr:country" ||
                   item.key == "addr:country_code") {
            if (item.value.length() == 2)
                countrycode = &item.value;
        } else if (boost::starts_with(item.key, "addr:") ||
                   item.key == "is_in" ||
                   boost::starts_with(item.key, "is_in:") ||
                   item.key == "tiger:county") {
            address.push_back(&item);
        } else if (item.key == "admin_level") {
            admin_level = atoi(item.value.c_str());
            if (admin_level <= 0 || admin_level > 100)
                admin_level = 100;
        } else if (item.key == "tracktype" ||
                   item.key == "traffic_calming" ||
                   item.key == "service" ||
                   item.key == "cuisine" ||
                   item.key == "capital" ||
                   item.key == "dispensing" ||
                   item.key == "religion" ||
                   item.key == "denomination" ||
                   item.key == "sport" ||
                   item.key == "internet_access" ||
                   item.key == "lanes" ||
                   item.key == "surface" ||
                   item.key == "smoothness" ||
                   item.key == "width" ||
                   item.key == "est_width" ||
                   item.key == "incline" ||
                   item.key == "opening_hours" ||
                   item.key == "collection_times" ||
                   item.key == "service_times" ||
                   item.key == "disused" ||
                   item.key == "wheelchair" ||
                   item.key == "sac_scale" ||
                   item.key == "trail_visibility" ||
                   item.key == "mtb:scale" ||
                   item.key == "mtb:description" ||
                   item.key == "wood" ||
                   item.key == "drive_through" ||
                   item.key == "drive_in" ||
                   item.key == "access" ||
                   item.key == "vehicle" ||
                   item.key == "bicyle" ||
                   item.key == "foot" ||
                   item.key == "goods" ||
                   item.key == "hgv" ||
                   item.key == "motor_vehicle" ||
                   item.key == "motor_car" ||
                   boost::starts_with(item.key, "access:") ||
                   boost::starts_with(item.key, "contact:") ||
                   boost::starts_with(item.key, "drink:") ||
                   item.key == "oneway" ||
                   item.key == "date_on" ||
                   item.key == "date_off" ||
                   item.key == "day_on" ||
                   item.key == "day_off" ||
                   item.key == "hour_on" ||
                   item.key == "hour_off" ||
                   item.key == "maxweight" ||
                   item.key == "maxheight" ||
                   item.key == "maxspeed" ||
                   item.key == "fee" ||
                   item.key == "toll" ||
                   boost::starts_with(item.key, "toll:") ||
                   item.key == "charge" ||
                   item.key == "population" ||
                   item.key == "description" ||
                   item.key == "image" ||
                   item.key == "attribution" ||
                   item.key == "fax" ||
                   item.key == "email" ||
                   item.key == "url" ||
                   item.key == "website" ||
                   item.key == "phone" ||
                   item.key == "real_ale" ||
                   item.key == "smoking" ||
                   item.key == "food" ||
                   item.key == "camera" ||
                   item.key == "brewery" ||
                   item.key == "locality" ||
                   item.key == "wikipedia" ||
                   boost::starts_with(item.key, "wikipedia:")) {
            extratags.push_back(&item);
        } else if (item.key == "building") {
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
        places.push_back(tag_t("place", "houses"));

    if (place) {
        if (isinterpolation ||
             (placeadmin &&
              place ->value != "island" &&
              place ->value != "islet"))
            extratags.push_back(place);
        else
            places.push_back(*place);
    }

    if (isnamed && places.empty()) {
        if (junction)
            places.push_back(*junction);
        else if (landuse)
            places.push_back(*landuse);
    }

    if (places.empty()) {
        if (placebuilding && (!names.empty() || placehouse || postcode)) {
            places.push_back(tag_t("building", "yes"));
        } else if (placehouse) {
            places.push_back(tag_t("place", "house"));
        } else if (postcode) {
            places.push_back(tag_t("place", "postcode"));
        }
    }

    // housenumbers
    if (!isinterpolation) {
        if (street_nr && conscr_nr) {
            housenumber.clear();
            escape(*conscr_nr, housenumber);
            housenumber.append("/");
            escape(*street_nr, housenumber);
        } else if (conscr_nr) {
            housenumber.clear();
            escape(*conscr_nr, housenumber);
        } else if (street_nr) {
            housenumber.clear();
            escape(*street_nr, housenumber);
        } else if (house_nr) {
            housenumber.clear();
            escape(*house_nr, housenumber);
        }
    }

}

void place_tag_processor::copy_out(char osm_type, osmid_t osm_id,
                                   const std::string &geom,
                                   std::string &buffer)
{
    for (const auto& place: places) {
        std::string name;
        if (place.key == "bridge" || place.key == "tunnel") {
            name = domain_name(place.key);
            if (name.empty())
                continue; // don't include unnamed bridges and tunnels
        }

        // osm_type
        buffer += osm_type;
        buffer += '\t';
        // osm_id
        buffer += (single_fmt % osm_id).str();
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
                if (!shop && (entry->key == "operator"))
                    continue;

                if (first)
                    first = false;
                else
                    buffer += ',';

                buffer += "\"";
                escape_array_record(entry->key, buffer);
                buffer += "\"=>\"";
                escape_array_record(entry->value, buffer);
                buffer += "\"";
            }
            buffer += '\t';
        } else
            buffer += "\\N\t";
        // admin_level
        buffer += (single_fmt % admin_level).str();
        // house number
        buffer += housenumber;
        buffer += '\t';
        // street
        copy_opt_string(street, buffer);
        // addr_place
        copy_opt_string(addr_place, buffer);
        // isin
        if (!address.empty()) {
            for (const auto entry: address) {
                if (entry->key == "tiger:county") {
                    escape(std::string(entry->value, 0, entry->value.find(",")),
                           buffer);
                    buffer += " county";
                } else {
                    escape(entry->value, buffer);
                }
                buffer += ',';
            }
            buffer[buffer.length() - 1] = '\t';
        } else
            buffer += "\\N\t";
        // postcode
        copy_opt_string(postcode, buffer);
        // country code
        copy_opt_string(countrycode, buffer);
        // extra tags
        if (extratags.empty()) {
            buffer += "\\N\t";
        } else {
            bool first = true;
            for (const auto entry: extratags) {
                if (first)
                    first = false;
                else
                    buffer += ',';

                buffer += "\"";
                escape_array_record(entry->key, buffer);
                buffer += "\"=>\"";
                escape_array_record(entry->value, buffer);
                buffer += "\"";
            }
            buffer += "\t";
        }
        // geometry
        buffer += srid_str;
        buffer += geom;
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
    PGresult *res = PQgetResult(Connection);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        std::cerr << "COPY_END for place failed: " << PQerrorMessage(Connection) << "\n";
        PQclear(res);
        util::exit_nicely();
    }

    /* Discard the result */
    PQclear(res);

    /* We no longer have an active copy */
    copy_active = false;
}



void output_gazetteer_t::delete_unused_classes(char osm_type, osmid_t osm_id) {
    char tmp2[2];
    tmp2[0] = osm_type; tmp2[1] = '\0';
    char const *paramValues[2];
    paramValues[0] = tmp2;
    paramValues[1] = (single_fmt % osm_id).str().c_str();
    PGresult *res = pgsql_execPrepared(ConnectionDelete, "get_classes", 2,
                                       paramValues, PGRES_TUPLES_OK);

    int sz = PQntuples(res);
    if (sz > 0 && !places.has_data()) {
        PQclear(res);
        /* unconditional delete of all places */
        delete_place(osm_type, osm_id);
    } else {
        std::string clslist;
        for (int i = 0; i < sz; i++) {
            std::string cls(PQgetvalue(res, i, 0));
            if (!places.has_place(cls)) {
                clslist.reserve(clslist.length() + cls.length() + 3);
                if (!clslist.empty())
                    clslist += ',';
                clslist += '\'';
                clslist += cls;
                clslist += '\'';
            }
        }

        PQclear(res);

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
            std::cerr << "Connection to database failed: " << PQerrorMessage(Connection) << "\n";
            return 1;
        }

        pgsql_exec(ConnectionDelete, PGRES_COMMAND_OK, "PREPARE get_classes (CHAR(1), " POSTGRES_OSMID_TYPE ") AS SELECT class FROM place WHERE osm_type = $1 and osm_id = $2");
    }
    return 0;
}

int output_gazetteer_t::start()
{
   reproj = m_options.projection;
   builder.set_exclude_broken_polygon(m_options.excludepoly);

   places.srid_str = (boost::format("SRID=%1%;") % SRID).str();

   if(connect())
       util::exit_nicely();

   /* Start a transaction */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "BEGIN");

   /* (Re)create the table unless we are appending */
   if (!m_options.append) {
      /* Drop any existing table */
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS place");

      /* Create the new table */
      if (m_options.tblsmain_data) {
          pgsql_exec(Connection, PGRES_COMMAND_OK,
                     CREATE_PLACE_TABLE, "TABLESPACE", m_options.tblsmain_data->c_str());
      } else {
          pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_TABLE, "", "");
      }
      if (m_options.tblsmain_index) {
          pgsql_exec(Connection, PGRES_COMMAND_OK,
                     CREATE_PLACE_ID_INDEX, "TABLESPACE", m_options.tblsmain_index->c_str());
      } else {
          pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_ID_INDEX, "", "");
      }

      pgsql_exec(Connection, PGRES_TUPLES_OK, "SELECT AddGeometryColumn('place', 'geometry', %d, 'GEOMETRY', 2)", SRID);
      pgsql_exec(Connection, PGRES_COMMAND_OK, "ALTER TABLE place ALTER COLUMN geometry SET NOT NULL");
   }

   return 0;
}


void output_gazetteer_t::stop()
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

int output_gazetteer_t::process_node(osmid_t id, double lat, double lon,
                                     const taglist_t &tags)
{
    places.process_tags(tags);

    if (m_options.append)
        delete_unused_classes('N', id);

    /* Are we interested in this item? */
    if (places.has_data()) {
        std::string wkt = (point_fmt % lon % lat).str();
        places.copy_out('N', id, wkt, buffer);
        flush_place_buffer();
    }

    return 0;
}

int output_gazetteer_t::process_way(osmid_t id, const idlist_t &nds, const taglist_t &tags)
{
    places.process_tags(tags);

    if (m_options.append)
        delete_unused_classes('W', id);

    /* Are we interested in this item? */
    if (places.has_data()) {
        /* Fetch the node details */
        nodelist_t nodes;
        m_mid->nodes_get_list(nodes, nds);

        /* Get the geometry of the object */
        auto geom = builder.get_wkb_simple(nodes, 1);
        if (geom.valid()) {
            places.copy_out('W', id, geom.geom, buffer);
            flush_place_buffer();
        }
    }

    return 0;
}

int output_gazetteer_t::process_relation(osmid_t id, const memberlist_t &members,
                                         const taglist_t &tags)
{
    const std::string *type = tags.get("type");
    if (!type) {
        delete_unused_full('R', id);
        return 0;
    }

    int cmp_waterway = type->compare("waterway");

    if (*type == "associatedStreet"
            || !(*type == "boundary" || *type == "multipolygon" || !cmp_waterway)) {
        delete_unused_full('R', id);
        return 0;
    }

    places.process_tags(tags);

    if (m_options.append)
        delete_unused_classes('R', id);

    /* Are we interested in this item? */
    if (!places.has_data())
        return 0;

    /* get the boundary path (ways) */
    idlist_t xid2;
    for (const auto& member: members) {
        /* only interested in ways */
        if (member.type == OSMTYPE_WAY)
            xid2.push_back(member.id);
    }

    if (xid2.empty()) {
        if (m_options.append)
            delete_unused_full('R', id);

        return 0;
    }

    multitaglist_t xtags;
    multinodelist_t xnodes;
    idlist_t xid;
    m_mid->ways_get_list(xid2, xid, xtags, xnodes);

    if (cmp_waterway) {
        auto geoms = builder.build_both(xnodes, 1, 1, 1000000, id);
        for (const auto& geom: geoms) {
            if (geom.is_polygon()) {
                places.copy_out('R', id, geom.geom, buffer);
                flush_place_buffer();
            } else {
                /* add_polygon_error('R', id, "boundary", "adminitrative", &names, countrycode, wkt); */
            }
        }
    } else {
        /* waterways result in multilinestrings */
        auto geom = builder.build_multilines(xnodes, id);
        if (geom.valid()) {
            places.copy_out('R', id, geom.geom, buffer);
            flush_place_buffer();
        }
    }

    return 0;
}


