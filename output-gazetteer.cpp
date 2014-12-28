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

enum tag_type {
 TAGINFO_NODE = 0x1u,
 TAGINFO_WAY = 0x2u,
 TAGINFO_AREA = 0x4u
};

enum { BUFFER_SIZE = 4092 };

void output_gazetteer_t::stop_copy(void)
{
    /* Do we have a copy active? */
    if (!CopyActive) return;

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
    CopyActive = 0;
}

static int split_tags(struct keyval *tags, unsigned int flags,
                      struct keyval *names, struct keyval *places,
                      struct keyval *extratags, int* admin_level,
                      struct keyval ** housenumber, struct keyval ** street,
                      struct keyval ** addr_place, char ** isin,
                      struct keyval ** postcode, struct keyval ** countrycode)
{
   size_t subval;
   int placehouse = 0;
   int placebuilding = 0;
   int placeadmin = 0;
   struct keyval *landuse;
   struct keyval *place;
   struct keyval *item;
   struct keyval *conscriptionnumber;
   struct keyval *streetnumber;

   *admin_level = ADMINLEVEL_NONE;
   *housenumber = 0;
   *street = 0;
   *addr_place = 0;
   *isin = 0;
   int isinsize = 0;
   *postcode = 0;
   *countrycode = 0;
   landuse = 0;
   place = 0;
   conscriptionnumber = 0;
   streetnumber = 0;

   /* Loop over the tags */
   while ((item = tags->popItem()) != NULL)
   {

      /* If this is a name tag, add it to the name list */
      if (item->key == "ref" ||
          item->key == "int_ref" ||
          item->key == "nat_ref" ||
          item->key == "reg_ref" ||
          item->key == "loc_ref" ||
          item->key == "old_ref" ||
          item->key == "ncn_ref" ||
          item->key == "rcn_ref" ||
          item->key == "lcn_ref" ||
          item->key == "iata" ||
          item->key == "icao" ||
          item->key == "pcode:1" ||
          item->key == "pcode:2" ||
          item->key == "pcode:3" ||
          item->key == "un:pcode:1" ||
          item->key == "un:pcode:2" ||
          item->key == "un:pcode:3" ||
          item->key == "name" ||
          (strncmp(item->key.c_str(), "name:", 5) == 0) ||
          item->key == "int_name" ||
          (strncmp(item->key.c_str(), "int_name:", 9) == 0) ||
          item->key == "nat_name" ||
          (strncmp(item->key.c_str(), "nat_name:", 9) == 0) ||
          item->key == "reg_name" ||
          (strncmp(item->key.c_str(), "reg_name:", 9) == 0) ||
          item->key == "loc_name" ||
          (strncmp(item->key.c_str(), "loc_name:", 9) == 0) ||
          item->key == "old_name" ||
          (strncmp(item->key.c_str(), "old_name:", 9) == 0) ||
          item->key == "alt_name" ||
          (strncmp(item->key.c_str(), "alt_name_", 9) == 0) ||
          (strncmp(item->key.c_str(), "alt_name:", 9) == 0) ||
          item->key == "official_name" ||
          (strncmp(item->key.c_str(), "official_name:", 14) == 0) ||
          item->key == "commonname" ||
          (strncmp(item->key.c_str(), "commonname:", 11) == 0) ||
          item->key == "common_name" ||
          (strncmp(item->key.c_str(), "common_name:", 12) == 0) ||
          item->key == "place_name" ||
          (strncmp(item->key.c_str(), "place_name:", 11) == 0) ||
          item->key == "short_name" ||
          (strncmp(item->key.c_str(), "short_name:", 11) == 0) ||
          item->key == "operator") /* operator is a bit of an oddity */
      {
         if (item->key == "name:prefix")
         {
            extratags->pushItem(item);
         }
         else
         {
            names->pushItem(item);
         }
      }
      else if (item->key == "emergency" ||
               item->key == "tourism" ||
               item->key == "historic" ||
               item->key == "military" ||
               item->key == "natural")
      {
         if (item->value != "no" && item->value != "yes")
         {
            places->pushItem(item);
         }
         else
         {
            delete(item);
         }
      }
      else if (item->key == "highway")
      {
         if (item->value != "no" &&
             item->value != "turning_circle" &&
             item->value != "traffic_signals" &&
             item->value != "mini_roundabout" &&
             item->value != "noexit" &&
             item->value != "crossing")
         {
             places->pushItem(item);
         }
         else
         {
             delete(item);
         }
      }
      else if (item->key == "aerialway" ||
               item->key == "aeroway" ||
               item->key == "amenity" ||
               item->key == "boundary" ||
               item->key == "bridge" ||
               item->key == "craft" ||
               item->key == "leisure" ||
               item->key == "office" ||
               item->key == "railway" ||
               item->key == "shop" ||
               item->key == "tunnel" )
      {
         if (item->value != "no")
         {
            places->pushItem(item);
            if (item->key == "boundary" && item->value == "administrative")
            {
               placeadmin = 1;
            }
         }
         else
         {
            delete(item);
         }
      }
      else if (item->key == "waterway" && item->value != "riverbank")
      {
            places->pushItem(item);
      }
      else if (item->key == "place")
      {
         place = item;
      }
      else if (item->key == "addr:housename")
      {
         names->pushItem(item);
         placehouse = 1;
      }
      else if (item->key == "landuse")
      {
         if (item->value == "cemetery")
            places->pushItem(item);
         else
            landuse = item;
      }
      else if (item->key == "postal_code" ||
          item->key == "post_code" ||
          item->key == "postcode" ||
          item->key == "addr:postcode" ||
          item->key == "tiger:zip_left" ||
          item->key == "tiger:zip_right")
      {
         if (*postcode)
	        delete(item);
         else
            *postcode = item;
      }
      else if (item->key == "addr:street")
      {
         *street = item;
      }
      else if (item->key == "addr:place")
      {
         *addr_place = item;
      }
      else if ((item->key == "country_code_iso3166_1_alpha_2" ||
                item->key == "country_code_iso3166_1" ||
                item->key == "country_code_iso3166" ||
                item->key == "country_code" ||
                item->key == "iso3166-1:alpha2" ||
                item->key == "iso3166-1" ||
                item->key == "ISO3166-1" ||
                item->key == "iso3166" ||
                item->key == "is_in:country_code" ||
                item->key == "addr:country" ||
                item->key == "addr:country_code")
                && item->value.length() == 2)
      {
         *countrycode = item;
      }
      else if (item->key == "addr:housenumber")
      {
          /* house number can be far more complex than just a single house number - leave for postgresql to deal with */
         if (*housenumber)
             delete(item);
         else {
             *housenumber = item;
             placehouse = 1;
         }
      }
      else if (item->key == "addr:conscriptionnumber")
      {
         if (conscriptionnumber)
             delete(item);
         else {
             conscriptionnumber = item;
             placehouse = 1;
         }
      }
      else if (item->key == "addr:streetnumber")
      {
         if (streetnumber)
             delete(item);
         else {
             streetnumber = item;
             placehouse = 1;
         }
      }
      else if (item->key == "addr:interpolation")
      {
          /* house number can be far more complex than just a single house number - leave for postgresql to deal with */
          if (*housenumber) {
              delete(item);
          } else {
             *housenumber = item;
             places->addItem("place", "houses", true);
          }
      }
      else if (item->key == "tiger:county")
      {
         /* strip the state and replace it with a county suffix to ensure that
            the tag only matches against counties and not against some town
            with the same name.
          */
         subval = strcspn(item->value.c_str(), ",");
         *isin = (char *)realloc(*isin, isinsize + 9 + subval);
         *(*isin+isinsize) = ',';
         strncpy(*isin+1+isinsize, item->value.c_str(), subval);
         strcpy(*isin+1+isinsize+subval, " county");
         isinsize += 8 + subval;
         delete(item);
      }
      else if (item->key == "is_in" ||
          (strncmp(item->key.c_str(), "is_in:", 5) == 0) ||
          item->key == "addr:suburb" ||
          item->key == "addr:county" ||
          item->key == "addr:city" ||
          item->key == "addr:state_code" ||
          item->key == "addr:state")
      {
          *isin = (char *)realloc(*isin, isinsize + 2 + item->value.length());
         *(*isin+isinsize) = ',';
         strcpy(*isin+1+isinsize, item->value.c_str());
         isinsize += 1 + item->value.length();
         delete(item);
      }
      else if (item->key == "admin_level")
      {
         *admin_level = atoi(item->value.c_str());
         delete(item);
      }
      else if (item->key == "tracktype" ||
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
               item->key == "food_hours" ||
               item->key == "collection_times" ||
               item->key == "service_times" ||
               item->key == "smoking_hours" ||
               item->key == "disused" ||
               item->key == "wheelchair" ||
               item->key == "sac_scale" ||
               item->key == "trail_visibility" ||
               item->key == "mtb:scale" ||
               item->key == "mtb:description" ||
               item->key == "wood" ||
               item->key == "drive_thru" ||
               item->key == "drive_in" ||
               item->key == "access" ||
               item->key == "vehicle" ||
               item->key == "bicyle" ||
               item->key == "foot" ||
               item->key == "goods" ||
               item->key == "hgv" ||
               item->key == "motor_vehicle" ||
               item->key == "motor_car" ||
               (strncmp(item->key.c_str(), "access:", 7) == 0) ||
               (strncmp(item->key.c_str(), "contact:", 8) == 0) ||
               (strncmp(item->key.c_str(), "drink:", 6) == 0) ||
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
               item->key == "disused" ||
               item->key == "toll" ||
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
               item->key == "tel" ||
               item->key == "real_ale" ||
               item->key == "smoking" ||
               item->key == "food" ||
               item->key == "camera" ||
               item->key == "brewery" ||
               item->key == "locality" ||
               item->key == "wikipedia" ||
               (strncmp(item->key.c_str(), "wikipedia:", 10) == 0)
               )
      {
          extratags->pushItem(item);
      }
      else if (item->key == "building")
      {
          placebuilding = 1;
          delete(item);
      }
      else if (item->key == "mountain_pass")
      {
          places->pushItem(item);
      }
      else
      {
         delete(item);
      }
   }

   /* Handle Czech/Slovak addresses:
        - if we have just a conscription number or a street number,
          just use the one we have as a house number
        - if we have both of them, concatenate them so users may search
          by any of them
    */
   if (conscriptionnumber || streetnumber)
   {
      if (*housenumber)
      {
         delete(*housenumber);
      }
      if (!conscriptionnumber)
      {
         streetnumber->key.assign("addr:housenumber");
         *housenumber = streetnumber;
      }
      if (!streetnumber)
      {
         conscriptionnumber->key.assign("addr:housenumber");
         *housenumber = conscriptionnumber;
      }
      if (conscriptionnumber && streetnumber)
      {
         conscriptionnumber->key.assign("addr:housenumber");
         conscriptionnumber->value.reserve(conscriptionnumber->value.size() + 1
                                           + streetnumber->value.size());
         conscriptionnumber->value += '/';
         conscriptionnumber->value += streetnumber->value;
         delete(streetnumber);
         *housenumber = conscriptionnumber;
      }
    }

   if (place)
   {
      if (placeadmin)
      {
         extratags->pushItem(place);
      }
      else
      {
         places->pushItem(place);
      }
   }

   if (placehouse && !places->listHasData())
   {
      places->addItem("place", "house", false);
   }

   /* Fallback place types - only used if we didn't create something more specific already */
   if (placebuilding && !places->listHasData() && (names->listHasData() || *housenumber || *postcode))
   {
      places->addItem("building", "yes", false);
   }

   if (landuse)
   {
      if (!places->listHasData() && names->listHasData())
      {
          places->pushItem(landuse);
      }
      else
      {
          delete(landuse);
      }
   }

   if (*postcode && !places->listHasData())
   {
      places->addItem("place", "postcode", false);
   }

   /* Try to convert everything to an area */
   return 1;
}


void output_gazetteer_t::delete_unused_classes(char osm_type, osmid_t osm_id, struct keyval *places) {
    char tmp2[2];
    tmp2[0] = osm_type; tmp2[1] = '\0';
    char const *paramValues[2];
    paramValues[0] = tmp2;
    paramValues[1] = (single_fmt % osm_id).str().c_str();
    PGresult *res = pgsql_execPrepared(ConnectionDelete, "get_classes", 2, paramValues, PGRES_TUPLES_OK);

    int sz = PQntuples(res);
    if (sz > 0 && !places) {
        PQclear(res);
        /* unconditional delete of all places */
        delete_place(osm_type, osm_id);
    } else {
        std::string clslist;
        for (int i = 0; i < sz; i++) {
            std::string cls(PQgetvalue(res, i, 0));
            if (!places->getItem(cls)) {
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
           pgsql_exec(Connection, PGRES_COMMAND_OK, "DELETE FROM place WHERE osm_type = '%c' AND osm_id = %"
        PRIdOSMID " and class = any(ARRAY[%s])", osm_type, osm_id, clslist.c_str());
        }
    }
}

void output_gazetteer_t::add_place(char osm_type, osmid_t osm_id, const std::string &key_class, const std::string &type, struct keyval *names, struct keyval *extratags,
   int adminlevel, struct keyval *housenumber, struct keyval *street, struct keyval *addr_place, const char *isin, struct keyval *postcode, struct keyval *countrycode, const std::string &wkt)
{
   /* Output a copy line for this place */
   buffer += osm_type;
   buffer += '\t';

   buffer += (single_fmt % osm_id).str();
   buffer += '\t';

   escape(key_class.c_str(), buffer);
   buffer += '\t';
   escape(type.c_str(), buffer);
   buffer += '\t';
   flush_place_buffer();

   /* start name array */
   hstore_to_place_table(names);

   buffer += (single_fmt % adminlevel).str();
   buffer += '\t';

   value_to_place_table(housenumber);
   value_to_place_table(street);
   value_to_place_table(addr_place);

   if (isin)
   {
       /* Skip the leading ',' from the contactination */
      escape(isin+1, buffer);
      buffer += "\t";
      flush_place_buffer();
   }
   else
      buffer += "\\N\t";

   value_to_place_table(postcode);
   value_to_place_table(countrycode);

   /* extra tags array */
   hstore_to_place_table(extratags);

   buffer += "SRID=";
   buffer += (single_fmt % SRID).str();
   buffer += wkt;

   buffer += '\n';
   flush_place_buffer();

   return;
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
    if (PQstatus(Connection) != CONNECTION_OK)
    {
       std::cerr << "Connection to database failed: " << PQerrorMessage(Connection) << "\n";
       return 1;
    }

    if(m_options.append) {
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

   if(connect())
       util::exit_nicely();

   /* Start a transaction */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "BEGIN");

   /* (Re)create the table unless we are appending */
   if (!m_options.append)
   {
      /* Drop any existing table */
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS place");

      /* Create the new table */
      if (m_options.tblsmain_data)
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK,
                     CREATE_PLACE_TABLE, "TABLESPACE", m_options.tblsmain_data->c_str());
      }
      else
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_TABLE, "", "");
      }
      if (m_options.tblsmain_index)
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK,
                     CREATE_PLACE_ID_INDEX, "TABLESPACE", m_options.tblsmain_index->c_str());
      }
      else
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_ID_INDEX, "", "");
      }

      pgsql_exec(Connection, PGRES_TUPLES_OK, "SELECT AddGeometryColumn('place', 'geometry', %d, 'GEOMETRY', 2)", SRID);
      pgsql_exec(Connection, PGRES_COMMAND_OK, "ALTER TABLE place ALTER COLUMN geometry SET NOT NULL");
   }

   return 0;
}

void output_gazetteer_t::commit()
{
}

void output_gazetteer_t::enqueue_ways(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
}

int output_gazetteer_t::pending_way(osmid_t id, int exists) {
    return 0;
}

void output_gazetteer_t::enqueue_relations(pending_queue_t &job_queue, osmid_t id, size_t output_id, size_t& added) {
}

int output_gazetteer_t::pending_relation(osmid_t id, int exists) {
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

int output_gazetteer_t::gazetteer_process_node(osmid_t id, double lat, double lon, struct keyval *tags, int delete_old)
{
   struct keyval names;
   struct keyval places;
   struct keyval extratags;
   struct keyval *place;
   int adminlevel;
   struct keyval * housenumber;
   struct keyval * street;
   struct keyval * addr_place;
   char * isin;
   struct keyval * postcode;
   struct keyval * countrycode;


   /* Split the tags */
   split_tags(tags, TAGINFO_NODE, &names, &places, &extratags, &adminlevel, &housenumber, &street, &addr_place, &isin, &postcode, &countrycode);

   if (delete_old)
       delete_unused_classes('N', id, &places);

   /* Are we interested in this item? */
   if (places.listHasData())
   {
      std::string wkt = (point_fmt % lon % lat).str();
      for (place = places.firstItem(); place; place = places.nextItem(place))
      {
         add_place('N', id, place->key, place->value, &names, &extratags, adminlevel, housenumber, street, addr_place, isin, postcode, countrycode, wkt);
      }
   }

   if (housenumber) delete(housenumber);
   if (street) delete(street);
   if (addr_place) delete(addr_place);
   if (isin) free(isin);
   if (postcode) delete(postcode);
   if (countrycode) delete(countrycode);

   /* Free tag lists */
   names.resetList();
   places.resetList();
   extratags.resetList();

   return 0;
}

int output_gazetteer_t::node_add(osmid_t id, double lat, double lon, struct keyval *tags)
{
    return gazetteer_process_node(id, lat, lon, tags, 0);
}

int output_gazetteer_t::gazetteer_process_way(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags, int delete_old)
{
   struct keyval names;
   struct keyval places;
   struct keyval extratags;
   struct keyval *place;
   int adminlevel;
   struct keyval * housenumber;
   struct keyval * street;
   struct keyval * addr_place;
   char * isin;
   struct keyval * postcode;
   struct keyval * countrycode;
   int area;


   /* Split the tags */
   area = split_tags(tags, TAGINFO_WAY, &names, &places, &extratags, &adminlevel, &housenumber, &street, &addr_place, &isin, &postcode, &countrycode);

   if (delete_old)
       delete_unused_classes('W', id, &places);

   /* Are we interested in this item? */
   if (places.listHasData())
   {
      struct osmNode *nodev;
      int nodec;

      /* Fetch the node details */
      nodev = (struct osmNode *)malloc(ndc * sizeof(struct osmNode));
      nodec = m_mid->nodes_get_list(nodev, ndv, ndc);

      /* Get the geometry of the object */
      geometry_builder::maybe_wkt_t wkt = builder.get_wkt_simple(nodev, nodec, area);
      if (wkt)
      {
         for (place = places.firstItem(); place; place = places.nextItem(place))
         {
            add_place('W', id, place->key, place->value, &names, &extratags, adminlevel,
                      housenumber, street, addr_place, isin, postcode, countrycode, wkt->geom);
         }
      }

      /* Free the nodes */
      free(nodev);
   }

   if (housenumber) delete(housenumber);
   if (street) delete(street);
   if (addr_place) delete(addr_place);
   if (isin) free(isin);
   if (postcode) delete(postcode);
   if (countrycode) delete(countrycode);

   /* Free tag lists */
   names.resetList();
   places.resetList();
   extratags.resetList();

   return 0;
}

int output_gazetteer_t::way_add(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags)
{
    return gazetteer_process_way(id, ndv, ndc, tags, 0);
}

int output_gazetteer_t::gazetteer_process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags, int delete_old)
{
   struct keyval names;
   struct keyval places;
   struct keyval extratags;
   struct keyval *place;
   int adminlevel;
   struct keyval * housenumber;
   struct keyval * street;
   struct keyval * addr_place;
   char * isin;
   struct keyval * postcode;
   struct keyval * countrycode;
   int cmp_waterway;

   const std::string *type = tags->getItem("type");
   if (!type) {
      if (delete_old) delete_unused_classes('R', id, 0);
      return 0;
   }

   cmp_waterway = type->compare("waterway");

   if (!type->compare("associatedStreet"))
   {
      if (delete_old) delete_unused_classes('R', id, 0);
      return 0;
   }

   if (type->compare("boundary") && type->compare("multipolygon") && cmp_waterway) {
      if (delete_old) delete_unused_classes('R', id, 0);
      return 0;
   }

   /* Split the tags */
   split_tags(tags, TAGINFO_AREA, &names, &places, &extratags, &adminlevel, &housenumber, &street, &addr_place, &isin, &postcode, &countrycode);

   /* reset type to NULL because split_tags() consumes the tags
    * keyval and means that it's pointing to some random stuff
    * which might be harmful if dereferenced. */
   type = NULL;

   if (delete_old)
       delete_unused_classes('R', id, &places);

   if (places.listHasData())
   {
      /* get the boundary path (ways) */
      int i, count;
      int *xcount = (int *)malloc( (member_count+1) * sizeof(int) );
      keyval *xtags  = new keyval[member_count+1];
      struct osmNode **xnodes = (struct osmNode **)malloc( (member_count+1) * sizeof(struct osmNode*) );
      osmid_t *xid2 = (osmid_t *)malloc( (member_count+1) * sizeof(osmid_t) );

      count = 0;
      for (i=0; i<member_count; i++)
      {
         /* only interested in ways */
         if (members[i].type != OSMTYPE_WAY)
            continue;
         xid2[count] = members[i].id;
         count++;
      }

      if (count == 0)
      {
          if (delete_old) delete_unused_classes('R', id, 0);
          free(xcount);
          delete [] xtags;
          free(xnodes);
          free(xid2);
          return 0;
      }

      osmid_t *xid = (osmid_t *)malloc( sizeof(osmid_t) * (count + 1));
      count = m_mid->ways_get_list(xid2, count, xid, xtags, xnodes, xcount);

      xnodes[count] = NULL;
      xcount[count] = 0;

      if (cmp_waterway)
      {
         geometry_builder::maybe_wkts_t wkts = builder.build_both(xnodes, xcount, 1, 1, 1000000, id);
         for (geometry_builder::wkt_itr wkt = wkts->begin(); wkt != wkts->end(); ++wkt)
         {
            if ((boost::starts_with(wkt->geom,  "POLYGON") || boost::starts_with(wkt->geom,  "MULTIPOLYGON")))
            {
                for (place = places.firstItem(); place; place = places.nextItem(place))
                {
                   add_place('R', id, place->key, place->value, &names, &extratags, adminlevel, housenumber, street, addr_place,
                             isin, postcode, countrycode, wkt->geom);
                }
            }
            else
            {
                /* add_polygon_error('R', id, "boundary", "adminitrative", &names, countrycode, wkt); */
            }
         }
      } else {
         /* waterways result in multilinestrings */
         // wkt_t build_multilines(const osmNode * const * xnodes, const int *xcount, osmid_t osm_id) const;
         geometry_builder::maybe_wkt_t wkt = builder.build_multilines(xnodes, xcount, id);
         if ((wkt->geom).length() > 0)
         {
            for (place = places.firstItem(); place; place = places.nextItem(place))
            {
               add_place('R', id, place->key, place->value, &names, &extratags, adminlevel, housenumber, street, addr_place,
                         isin, postcode, countrycode, wkt->geom);
            }
         }
      }
      for( i=0; i<count; i++ )
      {
         xtags[i].resetList();
         free( xnodes[i] );
      }

      free(xid);
      free(xid2);
      free(xcount);
      delete [] xtags;
      free(xnodes);
   }

   if (housenumber) delete(housenumber);
   if (street) delete(street);
   if (addr_place) delete(addr_place);
   if (isin) free(isin);
   if (postcode) delete(postcode);
   if (countrycode) delete(countrycode);

   /* Free tag lists */
   names.resetList();
   places.resetList();
   extratags.resetList();

   return 0;
}

int output_gazetteer_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
    return gazetteer_process_relation(id, members, member_count, tags, 0);
}

int output_gazetteer_t::node_delete(osmid_t id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this node */
   delete_place('N', id);

   return 0;
}

int output_gazetteer_t::way_delete(osmid_t id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this way */
   delete_place('W', id);

   return 0;
}

int output_gazetteer_t::relation_delete(osmid_t id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this relation */
   delete_place('R', id);

   return 0;
}

int output_gazetteer_t::node_modify(osmid_t id, double lat, double lon, struct keyval *tags)
{
   require_slim_mode();
   return gazetteer_process_node(id, lat, lon, tags, 1);
}

int output_gazetteer_t::way_modify(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags)
{
   require_slim_mode();
   return gazetteer_process_way(id, ndv, ndc, tags, 1);
}

int output_gazetteer_t::relation_modify(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
   require_slim_mode();
   return gazetteer_process_relation(id, members, member_count, tags, 1);
}


boost::shared_ptr<output_t> output_gazetteer_t::clone(const middle_query_t* cloned_middle) const {
    output_gazetteer_t *clone = new output_gazetteer_t(*this);
    clone->m_mid = cloned_middle;
    return boost::shared_ptr<output_t>(clone);
}

output_gazetteer_t::output_gazetteer_t(const middle_query_t* mid_, const options_t &options_)
    : output_t(mid_, options_),
      Connection(NULL),
      ConnectionDelete(NULL),
      ConnectionError(NULL),
      CopyActive(0),
      single_fmt("%1%"),
      point_fmt("POINT(%.15g %.15g)")
{
    buffer.reserve(BUFFER_SIZE);
}

output_gazetteer_t::output_gazetteer_t(const output_gazetteer_t& other)
    : output_t(other.m_mid, other.m_options),
      Connection(NULL),
      ConnectionDelete(NULL),
      ConnectionError(NULL),
      CopyActive(0),
      reproj(other.reproj),
      single_fmt(other.single_fmt),
      point_fmt(other.point_fmt)
{
    buffer.reserve(BUFFER_SIZE);
    builder.set_exclude_broken_polygon(m_options.excludepoly);
    connect();
}
