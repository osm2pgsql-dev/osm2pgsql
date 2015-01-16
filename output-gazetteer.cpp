#include <libpq-fe.h>
#include <boost/make_shared.hpp>
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
    src = NULL;
    admin_level = 0;
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
    bool operator()(const keyval *val) const {
        return val->key == "natural" ||
               val->key == "railway" ||
               val->key == "waterway" ||
               (val->key == "highway" && val->value == "traffic_signals");
    }
};

void place_tag_processor::process_tags(keyval *tags)
{
    bool placeadmin = false;
    bool placehouse = false;
    bool placebuilding = false;
    const keyval *place = 0;
    const keyval *junction = 0;
    const keyval *landuse = 0;
    bool isnamed = false;
    bool isinterpolation = false;
    const std::string *house_nr = 0;
    const std::string *conscr_nr = 0;
    const std::string *street_nr = 0;

    clear();
    src = tags;

    for (keyval *item = tags->firstItem(); item; item = tags->nextItem(item)) {
        if (item->key == "name:prefix") {
            extratags.push_back(item);
        } else if (item->key == "ref" ||
                   item->key == "int_ref" ||
                   item->key == "nat_ref" ||
                   item->key == "reg_ref" ||
                   item->key == "loc_ref" ||
                   item->key == "old_ref" ||
                   item->key == "iata" ||
                   item->key == "icao" ||
                   item->key == "operator" ||
                   item->key == "pcode" ||
                   boost::starts_with(item->key, "pcode:")) {
            names.push_back(item);
        } else if (item->key == "name" ||
                   boost::starts_with(item->key, "name:") ||
                   item->key == "int_name" ||
                   boost::starts_with(item->key, "int_name:") ||
                   item->key == "nat_name" ||
                   boost::starts_with(item->key, "nat_name:") ||
                   item->key == "reg_name" ||
                   boost::starts_with(item->key, "reg_name:") ||
                   item->key == "loc_name" ||
                   boost::starts_with(item->key, "loc_name:") ||
                   item->key == "old_name" ||
                   boost::starts_with(item->key, "old_name:") ||
                   item->key == "alt_name" ||
                   boost::starts_with(item->key, "alt_name:") ||
                   boost::starts_with(item->key, "alt_name_") ||
                   item->key == "official_name" ||
                   boost::starts_with(item->key, "official_name:") ||
                   item->key == "place_name" ||
                   boost::starts_with(item->key, "place_name:") ||
                   item->key == "short_name" ||
                   boost::starts_with(item->key, "short_name:") ||
                   item->key == "brand") {
            names.push_back(item);
            isnamed = true;
        } else if (item->key == "addr:housename") {
            names.push_back(item);
            placehouse = true;
        } else if (item->key == "emergency") {
            if (item->value != "fire_hydrant" &&
                item->value != "yes" &&
                item->value != "no")
                places.push_back(item);
        } else if (item->key == "tourism" ||
                   item->key == "historic" ||
                   item->key == "military" ||
                   item->key == "natural") {
            if (item->value != "no" && item->value != "yes")
                places.push_back(item);
        } else if (item->key == "landuse") {
            if (item->value == "cemetry")
                places.push_back(item);
            else
                landuse = item;
        } else if (item->key == "highway") {
            if (item->value != "no" &&
                item->value != "turning_circle" &&
                item->value != "mini_roundabout" &&
                item->value != "noexit" &&
                item->value != "crossing")
                places.push_back(item);
        } else if (item->key == "railway") {
            if (item->value != "level_crossing" &&
                item->value != "no")
                places.push_back(item);
        } else if (item->key == "man_made") {
            if (item->value != "survey_point" &&
                item->value != "cutline")
                places.push_back(item);
        } else if (item->key == "aerialway") {
            if (item->value != "pylon" &&
                item->value != "no")
                places.push_back(item);
        } else if (item->key == "boundary") {
            if (item->value == "administrative")
                placeadmin = true;
            places.push_back(item);
        } else if (item->key == "aeroway" ||
                   item->key == "amenity" ||
                   item->key == "boundary" ||
                   item->key == "bridge" ||
                   item->key == "craft" ||
                   item->key == "leisure" ||
                   item->key == "office" ||
                   item->key == "shop" ||
                   item->key == "tunnel" ||
                   item->key == "mountain_pass") {
            if (item->value != "no")
            {
                places.push_back(item);
            }
        } else if (item->key == "waterway") {
            if (item->value != "riverbank")
                places.push_back(item);
        } else if (item->key == "place") {
            place = item;
        } else if (item->key == "junction") {
            junction = item;
        } else if (item->key == "addr:interpolation") {
            housenumber.assign(item->value);
            isinterpolation = true;
        } else if (item->key == "addr:housenumber") {
            house_nr = &item->value;
            placehouse = true;
        } else if (item->key == "addr:conscriptionnumber") {
            conscr_nr = &item->value;
            placehouse = true;
        } else if (item->key == "addr:streetnumber") {
            street_nr = &item->value;
            placehouse = true;
        } else if (item->key == "addr:street") {
            street = &item->value;
        } else if (item->key == "addr:place") {
            addr_place = &item->value;
        } else if (item->key == "postal_code" ||
                   item->key == "postcode" ||
                   item->key == "addr:postcode" ||
                   item->key == "tiger:zip_left" ||
                   item->key == "tiger:zip_right") {
            if (!postcode)
                postcode = &item->value;
        } else if (item->key == "country_code" ||
                   item->key == "ISO3166-1" ||
                   item->key == "is_in:country_code" ||
                   item->key == "addr:country" ||
                   item->key == "addr:country_code") {
            if (item->value.length() == 2)
                countrycode = &item->value;
        } else if (boost::starts_with(item->key, "addr:") ||
                   item->key == "is_in" ||
                   boost::starts_with(item->key, "is_in:") ||
                   item->key == "tiger:county") {
            address.push_back(item);
        } else if (item->key == "admin_level") {
            admin_level = atoi(item->value.c_str());
        } else if (item->key == "tracktype" ||
                   item->key == "traffic_calming" ||
                   item->key == "service" ||
                   item->key == "cuisine" ||
                   item->key == "capital" ||
                   item->key == "dispensing" ||
                   item->key == "religion" ||
                   item->key == "denomination" ||
                   item->key == "sport" ||
                   item->key == "internet_access" ||
                   item->key == "lanes" ||
                   item->key == "surface" ||
                   item->key == "smoothness" ||
                   item->key == "width" ||
                   item->key == "est_width" ||
                   item->key == "incline" ||
                   item->key == "opening_hours" ||
                   item->key == "collection_times" ||
                   item->key == "service_times" ||
                   item->key == "disused" ||
                   item->key == "wheelchair" ||
                   item->key == "sac_scale" ||
                   item->key == "trail_visibility" ||
                   item->key == "mtb:scale" ||
                   item->key == "mtb:description" ||
                   item->key == "wood" ||
                   item->key == "drive_through" ||
                   item->key == "drive_in" ||
                   item->key == "access" ||
                   item->key == "vehicle" ||
                   item->key == "bicyle" ||
                   item->key == "foot" ||
                   item->key == "goods" ||
                   item->key == "hgv" ||
                   item->key == "motor_vehicle" ||
                   item->key == "motor_car" ||
                   boost::starts_with(item->key, "access:") ||
                   boost::starts_with(item->key, "contact:") ||
                   boost::starts_with(item->key, "drink:") ||
                   item->key == "oneway" ||
                   item->key == "date_on" ||
                   item->key == "date_off" ||
                   item->key == "day_on" ||
                   item->key == "day_off" ||
                   item->key == "hour_on" ||
                   item->key == "hour_off" ||
                   item->key == "maxweight" ||
                   item->key == "maxheight" ||
                   item->key == "maxspeed" ||
                   item->key == "fee" ||
                   item->key == "toll" ||
                   boost::starts_with(item->key, "toll:") ||
                   item->key == "charge" ||
                   item->key == "population" ||
                   item->key == "description" ||
                   item->key == "image" ||
                   item->key == "attribution" ||
                   item->key == "fax" ||
                   item->key == "email" ||
                   item->key == "url" ||
                   item->key == "website" ||
                   item->key == "phone" ||
                   item->key == "real_ale" ||
                   item->key == "smoking" ||
                   item->key == "food" ||
                   item->key == "camera" ||
                   item->key == "brewery" ||
                   item->key == "locality" ||
                   item->key == "wikipedia" ||
                   boost::starts_with(item->key, "wikipedia:")) {
            extratags.push_back(item);
        } else if (item->key == "building") {
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

    if (isinterpolation) {
        keyval *b = new keyval("place", "houses");
        src->pushItem(b); // to make sure it gets deleted
        places.push_back(b);
    }

    if (place) {
        if (isinterpolation ||
             (placeadmin &&
              place ->value != "island" &&
              place ->value != "islet"))
            extratags.push_back(place);
        else
            places.push_back(place);
    }

    if (isnamed && places.empty()) {
        if (junction)
            places.push_back(junction);
        else if (landuse)
            places.push_back(landuse);
    }

    if (places.empty()) {
        if (placebuilding && (!names.empty() || placehouse || postcode)) {
            keyval *b = new keyval("building", "yes");
            src->pushItem(b); // to make sure it gets deleted
            places.push_back(b);
        } else if (placehouse) {
            keyval *b = new keyval("place", "house");
            src->pushItem(b); // to make sure it gets deleted
            places.push_back(b);
        } else if (postcode) {
            keyval *b = new keyval("place", "postcode");
            src->pushItem(b); // to make sure it gets deleted
            places.push_back(b);
        }
    }

    // housenumbers
    if (!isinterpolation) {
        if (street_nr && conscr_nr) {
            housenumber.assign(*conscr_nr).append("/").append(*street_nr);
        } else if (conscr_nr) {
            housenumber.assign(*conscr_nr);
        } else if (street_nr) {
            housenumber.assign(*street_nr);
        } else if (house_nr) {
            housenumber.assign(*house_nr);
        }
    }

}

void place_tag_processor::copy_out(char osm_type, osmid_t osm_id,
                                   const std::string &wkt,
                                   std::string &buffer)
{
    BOOST_FOREACH(const keyval* place, places) {
        std::string name;
        if (place->key == "bridge" || place->key == "tunnel") {
            name = domain_name(place->key);
            if (name.empty())
                continue; // don't include unnamed bridges and tunnels
        }

        // osm_type
        buffer += osm_type;
        buffer += '\t';
        // osm_id
        buffer += (single_fmt % osm_id).str();
        // class
        escape(place->key, buffer);
        buffer += '\t';
        // type
        escape(place->value, buffer);
        buffer += '\t';
        // names
        if (!name.empty()) {
            buffer += name;
            buffer += '\t';
        } else if (!names.empty()) {
            bool first = true;
            // operator will be ignored on anything but these classes
            // (amenity for restaurant and fuel)
            bool shop = (place->key == "shop") ||
                        (place->key == "amenity") ||
                        (place->key == "tourism");
            BOOST_FOREACH(const keyval *entry, names) {
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
            BOOST_FOREACH(const keyval *entry, address) {
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
            BOOST_FOREACH(const keyval *entry, extratags) {
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
        // wkt
        buffer += srid_str;
        buffer += wkt;
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
    if (PQputCopyEnd(Connection, NULL) != 1)
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
    Connection = PQconnectdb(m_options.conninfo.c_str());

    /* Check to see that the backend connection was successfully made */
    if (PQstatus(Connection) != CONNECTION_OK) {
       std::cerr << "Connection to database failed: " << PQerrorMessage(Connection) << "\n";
       return 1;
    }

    if (m_options.append) {
        ConnectionDelete = PQconnectdb(m_options.conninfo.c_str());
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
                                     struct keyval *tags)
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

int output_gazetteer_t::process_way(osmid_t id, osmid_t *ndv, int ndc,
                                    struct keyval *tags)
{
    places.process_tags(tags);

    if (m_options.append)
        delete_unused_classes('W', id);

    /* Are we interested in this item? */
    if (places.has_data()) {
        struct osmNode *nodev;
        int nodec;

        /* Fetch the node details */
        nodev = (struct osmNode *)malloc(ndc * sizeof(struct osmNode));
        nodec = m_mid->nodes_get_list(nodev, ndv, ndc);

        /* Get the geometry of the object */
        geometry_builder::maybe_wkt_t wkt = builder.get_wkt_simple(nodev, nodec, 1);
        if (wkt) {
            places.copy_out('W', id, wkt->geom, buffer);
            flush_place_buffer();
        }

        /* Free the nodes */
        free(nodev);
    }

    return 0;
}

int output_gazetteer_t::process_relation(osmid_t id, struct member *members,
        int member_count, struct keyval *tags)
{
    int cmp_waterway;

    const std::string *type = tags->getItem("type");
    if (!type) {
        places.clear();
        if (m_options.append) delete_unused_classes('R', id);
        return 0;
    }

    cmp_waterway = type->compare("waterway");

    if (*type == "associatedStreet"
            || !(*type == "boundary" || *type == "multipolygon" || !cmp_waterway)) {
        places.clear();
        if (m_options.append) delete_unused_classes('R', id);
        return 0;
    }

    places.process_tags(tags);

    if (m_options.append)
        delete_unused_classes('R', id);

    /* Are we interested in this item? */
    if (!places.has_data())
        return 0;

    /* get the boundary path (ways) */
    osmid_t *xid2 = new osmid_t[member_count+1];

    int count = 0;
    for (int i=0; i<member_count; ++i) {
        /* only interested in ways */
        if (members[i].type != OSMTYPE_WAY)
            continue;
        xid2[count] = members[i].id;
        count++;
    }

    if (count == 0) {
        if (m_options.append)
            delete_unused_classes('R', id);

        delete [] xid2;

        return 0;
    }

    int *xcount = new int[count + 1];
    keyval *xtags  = new keyval[count+1];
    struct osmNode **xnodes = new osmNode*[count + 1];
    osmid_t *xid = new osmid_t[count + 1];
    count = m_mid->ways_get_list(xid2, count, xid, xtags, xnodes, xcount);

    xnodes[count] = NULL;
    xcount[count] = 0;

    if (cmp_waterway) {
        geometry_builder::maybe_wkts_t wkts = builder.build_both(xnodes, xcount, 1, 1, 1000000, id);
        for (geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt) {
            if (boost::starts_with(wkt->geom,  "POLYGON")
                    || boost::starts_with(wkt->geom,  "MULTIPOLYGON")) {
                places.copy_out('R', id, wkt->geom, buffer);
                flush_place_buffer();
            } else {
                /* add_polygon_error('R', id, "boundary", "adminitrative", &names, countrycode, wkt); */
            }
        }
    } else {
        /* waterways result in multilinestrings */
        geometry_builder::maybe_wkt_t wkt = builder.build_multilines(xnodes, xcount, id);
        if ((wkt->geom).length() > 0) {
            places.copy_out('R', id, wkt->geom, buffer);
            flush_place_buffer();
        }
    }

    for (int i=0; i<count; ++i)
    {
        xtags[i].resetList();
        free(xnodes[i]);
    }

    free(xid);
    delete [] xid2;
    delete [] xcount;
    delete [] xtags;
    delete [] xnodes;

    return 0;
}


