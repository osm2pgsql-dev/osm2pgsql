#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpq-fe.h>

#include "osmtypes.h"
#include "middle.h"
#include "output.h"
#include "output-gazetteer.h"
#include "pgsql.h"
#include "reprojection.h"
#include "build_geometry.h"

#define BUFFER_SIZE 4096

#define SRID (project_getprojinfo()->srs)

#define CREATE_KEYVALUETYPE_TYPE                \
   "CREATE TYPE keyvalue AS ("                  \
   "  key TEXT,"                                \
   "  value TEXT"                               \
   ")"

#define CREATE_WORDSCORE_TYPE                   \
   "CREATE TYPE wordscore AS ("                 \
   "  word TEXT,"                                \
   "  score FLOAT"                               \
   ")"

#define CREATE_PLACE_TABLE                   \
   "CREATE TABLE place ("                       \
   "  osm_type CHAR(1) NOT NULL,"               \
   "  osm_id " POSTGRES_OSMID_TYPE " NOT NULL," \
   "  class TEXT NOT NULL,"                     \
   "  type TEXT NOT NULL,"                      \
   "  name HSTORE,"                             \
   "  admin_level INTEGER,"                     \
   "  housenumber TEXT,"                        \
   "  street TEXT,"                             \
   "  isin TEXT,"                               \
   "  postcode TEXT,"                           \
   "  country_code VARCHAR(2),"                 \
   "  extratags HSTORE"                         \
   ") %s %s"

#define ADMINLEVEL_NONE 100

#define CREATE_PLACE_ID_INDEX \
   "CREATE INDEX place_id_idx ON place USING BTREE (osm_type, osm_id) %s %s"

#define TAGINFO_NODE 0x1u
#define TAGINFO_WAY  0x2u
#define TAGINFO_AREA 0x4u

//static int gazetteer_delete_relation(osmid_t osm_id);

static const struct output_options *Options = NULL;
static PGconn *Connection = NULL;
static int CopyActive = 0;
static char Buffer[BUFFER_SIZE];
static unsigned int BufferLen = 0;

static PGconn *ConnectionDelete = NULL;

static PGconn *ConnectionError = NULL;
static int CopyErrorActive = 0;
static char BufferError[BUFFER_SIZE];
static unsigned int BufferErrorLen = 0;

static FILE * hLog = NULL;

static void require_slim_mode(void)
{
   if (!Options->slim)
   {
      fprintf(stderr, "Cannot apply diffs unless in slim mode\n");
      exit_nicely();
   }

   return;
}

static void copy_data(const char *sql)
{
   unsigned int sqlLen = strlen(sql);

   /* Make sure we have an active copy */
   if (!CopyActive)
   {
      pgsql_exec(Connection, PGRES_COPY_IN, "COPY place FROM STDIN");
      CopyActive = 1;
   }

   /* If the combination of old and new data is too big, flush old data */
   if (BufferLen + sqlLen > BUFFER_SIZE - 10)
   {
      pgsql_CopyData("place", Connection, Buffer);
      BufferLen = 0;
   }

   /*
    * If new data by itself is too big, output it immediately,
    * otherwise just add it to the buffer.
    */
   if (sqlLen > BUFFER_SIZE - 10)
   {
      pgsql_CopyData("Place", Connection, sql);
      sqlLen = 0;
   }
   else if (sqlLen > 0)
   {
      strcpy(Buffer + BufferLen, sql);
      BufferLen += sqlLen;
      sqlLen = 0;
   }

   /* If we have completed a line, output it */
   if (BufferLen > 0 && Buffer[BufferLen-1] == '\n')
   {
      pgsql_CopyData("place", Connection, Buffer);
      BufferLen = 0;
   }

   return;
}

static void stop_copy(void)
{
   PGresult *res;

   /* Do we have a copy active? */
   if (!CopyActive) return;

   /* Terminate the copy */
   if (PQputCopyEnd(Connection, NULL) != 1)
   {
      fprintf(stderr, "COPY_END for place failed: %s\n", PQerrorMessage(Connection));
      exit_nicely();
   }

   /* Check the result */
   res = PQgetResult(Connection);
   if (PQresultStatus(res) != PGRES_COMMAND_OK)
   {
      fprintf(stderr, "COPY_END for place failed: %s\n", PQerrorMessage(Connection));
      PQclear(res);
      exit_nicely();
   }

   /* Discard the result */
   PQclear(res);

   /* We no longer have an active copy */
   CopyActive = 0;

   return;
}

static void copy_error_data(const char *sql)
{
   unsigned int sqlLen = strlen(sql);

   if (hLog) fprintf(hLog, "%s", sql);

   /* Make sure we have an active copy */
   if (!CopyErrorActive)
   {
      pgsql_exec(ConnectionError, PGRES_COPY_IN, "COPY import_polygon_error (osm_type, osm_id, class, type, name, country_code, updated, errormessage, prevgeometry, newgeometry) FROM stdin;");
      CopyErrorActive = 1;
   }

   /* If the combination of old and new data is too big, flush old data */
   if (BufferErrorLen + sqlLen > BUFFER_SIZE - 10)
   {
      pgsql_CopyData("import_polygon_error", ConnectionError, BufferError);
      BufferErrorLen = 0;
   }

   /*
    * If new data by itself is too big, output it immediately,
    * otherwise just add it to the buffer.
    */
   if (sqlLen > BUFFER_SIZE - 10)
   {
      pgsql_CopyData("import_polygon_error", ConnectionError, sql);
      sqlLen = 0;
   }
   else if (sqlLen > 0)
   {
      strcpy(BufferError + BufferErrorLen, sql);
      BufferErrorLen += sqlLen;
      sqlLen = 0;
   }

   /* If we have completed a line, output it */
   if (BufferErrorLen > 0 && BufferError[BufferErrorLen-1] == '\n')
   {
      pgsql_CopyData("place", ConnectionError, BufferError);
      BufferErrorLen = 0;
   }

   return;
}

static void stop_error_copy(void)
{
   PGresult *res;

   /* Do we have a copy active? */
   if (!CopyErrorActive) return;

   /* Terminate the copy */
   if (PQputCopyEnd(ConnectionError, NULL) != 1)
   {
      fprintf(stderr, "COPY_END for import_polygon_error failed: %s\n", PQerrorMessage(ConnectionError));
      exit_nicely();
   }

   /* Check the result */
   res = PQgetResult(ConnectionError);
   if (PQresultStatus(res) != PGRES_COMMAND_OK)
   {
      fprintf(stderr, "COPY_END for import_polygon_error failed: %s\n", PQerrorMessage(ConnectionError));
      PQclear(res);
      exit_nicely();
   }

   /* Discard the result */
   PQclear(res);

   /* We no longer have an active copy */
   CopyErrorActive = 0;

   return;
}

static int split_tags(struct keyval *tags, unsigned int flags, struct keyval *names, struct keyval *places, struct keyval *extratags, 
   int* admin_level, struct keyval ** housenumber, struct keyval ** street, char ** isin, struct keyval ** postcode, struct keyval ** countrycode)
{
   int placehouse = 0;
   int placebuilding = 0;
   struct keyval *landuse;
   struct keyval *place;
   struct keyval *item;

   *admin_level = ADMINLEVEL_NONE;
   *housenumber = 0;
   *street = 0;
   *isin = 0;
   int isinsize = 0;
   *postcode = 0;
   *countrycode = 0;
   landuse = 0;
   place = 0;

   /* Initialise the result lists */
   initList(names);
   initList(places);
   initList(extratags);

   /* Loop over the tags */
   while ((item = popItem(tags)) != NULL)
   {
//      fprintf(stderr, "%s\n", item->key);

      /* If this is a name tag, add it to the name list */
      if (strcmp(item->key, "ref") == 0 ||
          strcmp(item->key, "int_ref") == 0 ||
          strcmp(item->key, "nat_ref") == 0 ||
          strcmp(item->key, "reg_ref") == 0 ||
          strcmp(item->key, "loc_ref") == 0 ||
          strcmp(item->key, "old_ref") == 0 ||
          strcmp(item->key, "ncn_ref") == 0 ||
          strcmp(item->key, "rcn_ref") == 0 ||
          strcmp(item->key, "lcn_ref") == 0 ||
          strcmp(item->key, "iata") == 0 ||
          strcmp(item->key, "icao") == 0 ||
          strcmp(item->key, "pcode:1") == 0 ||
          strcmp(item->key, "pcode:2") == 0 ||
          strcmp(item->key, "pcode:3") == 0 ||
          strcmp(item->key, "un:pcode:1") == 0 ||
          strcmp(item->key, "un:pcode:2") == 0 ||
          strcmp(item->key, "un:pcode:3") == 0 ||
          strcmp(item->key, "name") == 0 ||
          (strncmp(item->key, "name:", 5) == 0) ||
          strcmp(item->key, "int_name") == 0 ||
          (strncmp(item->key, "int_name:", 9) == 0) || 
          strcmp(item->key, "nat_name") == 0 ||
          (strncmp(item->key, "nat_name:", 9) == 0) || 
          strcmp(item->key, "reg_name") == 0 ||
          (strncmp(item->key, "reg_name:", 9) == 0) || 
          strcmp(item->key, "loc_name") == 0 ||
          (strncmp(item->key, "loc_name:", 9) == 0) || 
          strcmp(item->key, "old_name") == 0 ||
          (strncmp(item->key, "old_name:", 9) == 0) || 
          strcmp(item->key, "alt_name") == 0 ||
          (strncmp(item->key, "alt_name:", 9) == 0) || 
          strcmp(item->key, "official_name") == 0 ||
          (strncmp(item->key, "official_name:", 14) == 0) || 
          strcmp(item->key, "commonname") == 0 ||
          (strncmp(item->key, "commonname:", 11) == 0) ||
          strcmp(item->key, "common_name") == 0 ||
          (strncmp(item->key, "common_name:", 12) == 0) ||
          strcmp(item->key, "place_name") == 0 ||
          (strncmp(item->key, "place_name:", 11) == 0) ||
          strcmp(item->key, "short_name") == 0 ||
          (strncmp(item->key, "short_name:", 11) == 0) ||
          strcmp(item->key, "operator") == 0) //operator is a bit of an oddity
      {
         pushItem(names, item);
      }
      else if (strcmp(item->key, "aeroway") == 0 ||
               strcmp(item->key, "amenity") == 0 ||
               strcmp(item->key, "boundary") == 0 ||
               strcmp(item->key, "bridge") == 0 ||
               strcmp(item->key, "craft") == 0 ||
               strcmp(item->key, "emergency") == 0 ||
               strcmp(item->key, "highway") == 0 ||
               strcmp(item->key, "historic") == 0 ||
               strcmp(item->key, "leisure") == 0 ||
               strcmp(item->key, "military") == 0 ||
               strcmp(item->key, "natural") == 0 ||
               strcmp(item->key, "office") == 0 ||
               strcmp(item->key, "railway") == 0 ||
               strcmp(item->key, "shop") == 0 ||
               strcmp(item->key, "tourism") == 0 ||
               strcmp(item->key, "tunnel") == 0 ||
               strcmp(item->key, "waterway") == 0 )
      {
         if (strcmp(item->value, "no"))
         {
            pushItem(places, item);
         }
         else
         {
            freeItem(item);
         }
      }
      else if (strcmp(item->key, "place") == 0) 
      {
         place = item;
      }
      else if (strcmp(item->key, "addr:housename") == 0)
      {
         pushItem(names, item);
         placehouse = 1;
      }
      else if (strcmp(item->key, "landuse") == 0)
      {
         landuse = item;
      }
      else if (strcmp(item->key, "postal_code") == 0 ||
          strcmp(item->key, "post_code") == 0 ||
          strcmp(item->key, "postcode") == 0 ||
          strcmp(item->key, "addr:postcode") == 0 ||
          strcmp(item->key, "tiger:zip_left") == 0 ||
          strcmp(item->key, "tiger:zip_right") == 0)
      {
         if (*postcode)
	        freeItem(item);
         else
            *postcode = item;
      }
      else if (strcmp(item->key, "addr:street") == 0)
      {
         *street = item;
      }
      else if ((strcmp(item->key, "country_code_iso3166_1_alpha_2") == 0 || 
                strcmp(item->key, "country_code_iso3166_1") == 0 || 
                strcmp(item->key, "country_code_iso3166") == 0 || 
                strcmp(item->key, "country_code") == 0 || 
                strcmp(item->key, "iso3166-1:alpha2") == 0 || 
                strcmp(item->key, "iso3166-1") == 0 || 
                strcmp(item->key, "ISO3166-1") == 0 || 
                strcmp(item->key, "iso3166") == 0 || 
                strcmp(item->key, "is_in:country_code") == 0 || 
                strcmp(item->key, "addr:country") == 0 ||
                strcmp(item->key, "addr:country_code") == 0) 
                && strlen(item->value) == 2)
      {
         *countrycode = item;
      }
      else if (strcmp(item->key, "addr:housenumber") == 0)
      {
         // house number can be far more complex than just a single house number - leave for postgresql to deal with
         if (*housenumber)
             freeItem(item);
         else {
             *housenumber = item;
             placehouse = 1;
         }
      }
      else if (strcmp(item->key, "addr:interpolation") == 0)
      {
         // house number can be far more complex than just a single house number - leave for postgresql to deal with
          if (*housenumber) {
              freeItem(item);
          } else {
             *housenumber = item; 
             addItem(places, "place", "houses", 1);
          }
      }
      else if (strcmp(item->key, "is_in") == 0 ||
          (strncmp(item->key, "is_in:", 5) == 0) ||
          strcmp(item->key, "addr:country")== 0 ||
          strcmp(item->key, "addr:county")== 0 ||
          strcmp(item->key, "tiger:county")== 0 ||
          strcmp(item->key, "addr:city") == 0 ||
          strcmp(item->key, "addr:state_code") == 0 ||
          strcmp(item->key, "addr:state") == 0)
      {
         *isin = realloc(*isin, isinsize + 2 + strlen(item->value));
         *(*isin+isinsize) = ',';
         strcpy(*isin+1+isinsize, item->value);
         isinsize += 1 + strlen(item->value);
         freeItem(item);
      }
      else if (strcmp(item->key, "admin_level") == 0)
      {
         *admin_level = atoi(item->value);
         freeItem(item);
      }
      else if (strcmp(item->key, "tracktype") == 0 ||
               strcmp(item->key, "traffic_calming") == 0 ||
               strcmp(item->key, "service") == 0 ||
               strcmp(item->key, "cuisine") == 0 ||
               strcmp(item->key, "capital") == 0 ||
               strcmp(item->key, "dispensing") == 0 ||
               strcmp(item->key, "religion") == 0 ||
               strcmp(item->key, "denomination") == 0 ||
               strcmp(item->key, "sport") == 0 ||
               strcmp(item->key, "internet_access") == 0 ||
               strcmp(item->key, "lanes") == 0 ||
               strcmp(item->key, "surface") == 0 ||
               strcmp(item->key, "smoothness") == 0 ||
               strcmp(item->key, "width") == 0 ||
               strcmp(item->key, "est_width") == 0 ||
               strcmp(item->key, "incline") == 0 ||
               strcmp(item->key, "opening_hours") == 0 ||
               strcmp(item->key, "food_hours") == 0 ||
               strcmp(item->key, "collection_times") == 0 ||
               strcmp(item->key, "service_times") == 0 ||
               strcmp(item->key, "smoking_hours") == 0 ||
               strcmp(item->key, "disused") == 0 ||
               strcmp(item->key, "wheelchair") == 0 ||
               strcmp(item->key, "sac_scale") == 0 ||
               strcmp(item->key, "trail_visibility") == 0 ||
               strcmp(item->key, "mtb:scale") == 0 ||
               strcmp(item->key, "mtb:description") == 0 ||
               strcmp(item->key, "wood") == 0 ||
               strcmp(item->key, "drive_thru") == 0 ||
               strcmp(item->key, "drive_in") == 0 ||
               strcmp(item->key, "access") == 0 ||
               strcmp(item->key, "vehicle") == 0 ||
               strcmp(item->key, "bicyle") == 0 ||
               strcmp(item->key, "foot") == 0 ||
               strcmp(item->key, "goods") == 0 ||
               strcmp(item->key, "hgv") == 0 ||
               strcmp(item->key, "motor_vehicle") == 0 ||
               strcmp(item->key, "motor_car") == 0 ||
               (strncmp(item->key, "access:", 7) == 0) ||
               (strncmp(item->key, "contact:", 8) == 0) ||
               (strncmp(item->key, "drink:", 6) == 0) ||
               strcmp(item->key, "oneway") == 0 ||
               strcmp(item->key, "date_on") == 0 ||
               strcmp(item->key, "date_off") == 0 ||
               strcmp(item->key, "day_on") == 0 ||
               strcmp(item->key, "day_off") == 0 ||
               strcmp(item->key, "hour_on") == 0 ||
               strcmp(item->key, "hour_off") == 0 ||
               strcmp(item->key, "maxweight") == 0 ||
               strcmp(item->key, "maxheight") == 0 ||
               strcmp(item->key, "speed") == 0 ||
               strcmp(item->key, "disused") == 0 ||
               strcmp(item->key, "toll") == 0 ||
               strcmp(item->key, "charge") == 0 ||
               strcmp(item->key, "population") == 0 ||
               strcmp(item->key, "description") == 0 ||
               strcmp(item->key, "image") == 0 ||
               strcmp(item->key, "attribution") == 0 ||
               strcmp(item->key, "fax") == 0 ||
               strcmp(item->key, "email") == 0 ||
               strcmp(item->key, "url") == 0 ||
               strcmp(item->key, "website") == 0 ||
               strcmp(item->key, "phone") == 0 ||
               strcmp(item->key, "tel") == 0 ||
               strcmp(item->key, "real_ale") == 0 ||
               strcmp(item->key, "smoking") == 0 ||
               strcmp(item->key, "food") == 0 ||
               strcmp(item->key, "camera") == 0 ||
               strcmp(item->key, "brewery") == 0 ||
               strcmp(item->key, "locality") == 0 ||
               strcmp(item->key, "wikipedia") == 0 ||
               (strncmp(item->key, "wikipedia:", 10) == 0)
               )
      {
          pushItem(extratags, item);
      }
      else if (strcmp(item->key, "building") == 0)
      {
          placebuilding = 1;
          freeItem(item);
      }
      else if (strcmp(item->key, "mountain_pass") == 0)
      {
          // the key be mountain_pass only ever comes with the value Yes.
          // Not helpful. Therefore "retag" to place=mountain_pass
          addItem(places, "place", "mountain_pass", 1);
          freeItem(item);
      }
      else
      {
         freeItem(item);
      }
   }

   if (place)
   {
      if (listHasData(places) && (*admin_level != ADMINLEVEL_NONE))
      {
         pushItem(extratags, place);
      } 
      else
      {
         pushItem(places, place);
      }
   }

   if (placehouse && !listHasData(places))
   {
      addItem(places, "place", "house", 1);
   }

   // Fallback place types - only used if we didn't create something more specific already
   if (placebuilding && !listHasData(places))
   {
      addItem(places, "building", "yes", 1);
   }

   if (landuse)
   {
      if (!listHasData(places))
      {
          pushItem(places, landuse);
      }
      else
      {
          freeItem(item);
      }
   }

   if (*postcode && !listHasData(places))
   {
      addItem(places, "place", "postcode", 1);
   }

   // Try to convert everything to an area
   return 1;
}

void escape_array_record(char *out, int len, const char *in)
{
    int count = 0;
    const char *old_in = in, *old_out = out;

    if (!len)
        return;

    while(*in && count < len-3) {
        switch(*in) {
            case '\\': *out++ = '\\'; *out++ = '\\'; *out++ = '\\'; *out++ = '\\'; *out++ = '\\'; *out++ = '\\'; *out++ = '\\'; *out++ = '\\'; count+= 8; break;
            case '\n': 
            case '\r': 
            case '\t': 
            case '"': 
		// This is a bit naughty - we know that nominatim ignored these characters so just drop them now for simplicity
		*out++ = ' '; count++; break;
            default:   *out++ = *in; count++; break;
        }
        in++;
    }
    *out = '\0';

    if (*in)
        fprintf(stderr, "%s truncated at %d chars: %s\n%s\n", __FUNCTION__, count, old_in, old_out);
}

static void delete_unused_classes(char osm_type, osmid_t osm_id, struct keyval *places) {
    int i,sz, slen;
    PGresult   *res;
    char tmp[16];
    char tmp2[2];
    char *cls, *clslist = 0;
    char const *paramValues[2];
    
    tmp2[0] = osm_type; tmp2[1] = '\0';
    paramValues[0] = tmp2;
    snprintf(tmp, sizeof(tmp), "%" PRIdOSMID, osm_id);
    paramValues[1] = tmp;
    res = pgsql_execPrepared(ConnectionDelete, "get_classes", 2, paramValues, PGRES_TUPLES_OK);

    sz = PQntuples(res);
    if (sz > 0 && !places) {
        PQclear(res);
        /* uncondtional delete of all places */
        stop_copy();
        pgsql_exec(Connection, PGRES_COMMAND_OK, "DELETE FROM place WHERE osm_type = '%c' AND osm_id  = %" PRIdOSMID, osm_type, osm_id);
    } else {
        for (i = 0; i < sz; i++) {
            cls = PQgetvalue(res, i, 0);
            if (!getItem(places, cls)) {
                if (!clslist) {
                    clslist = malloc(strlen(cls)+3);
                    sprintf(clslist, "'%s'", cls);
                } else {
                    slen = strlen(clslist);
                    clslist = realloc(clslist, slen + 4 + strlen(cls));
                    sprintf(&(clslist[slen]), ",'%s'", cls); 
                }
            }
        }

        PQclear(res);

        if (clslist) {
           /* Stop any active copy */
           stop_copy();

           /* Delete all places for this object */
           pgsql_exec(Connection, PGRES_COMMAND_OK, "DELETE FROM place WHERE osm_type = '%c' AND osm_id = %"
        PRIdOSMID " and class = any(ARRAY[%s])", osm_type, osm_id, clslist);
           free(clslist);
        }
    }
}

static void add_place(char osm_type, osmid_t osm_id, const char *class, const char *type, struct keyval *names, struct keyval *extratags,
   int adminlevel, struct keyval *housenumber, struct keyval *street, const char *isin, struct keyval *postcode, struct keyval *countrycode, const char *wkt)
{
   int first;
   struct keyval *name;
   char sql[2048];

   /* Output a copy line for this place */
   sprintf(sql, "%c\t%" PRIdOSMID "\t", osm_type, osm_id);
   copy_data(sql);

   escape(sql, sizeof(sql), class);
   copy_data(sql);
   copy_data("\t");

   escape(sql, sizeof(sql), type);
   copy_data(sql);
   copy_data("\t");

   /* start name array */
   if (listHasData(names))
   {
      first = 1;
      for (name = firstItem(names); name; name = nextItem(names, name))
      {
         if (first) first = 0;
         else copy_data(", ");

         copy_data("\"");

         escape_array_record(sql, sizeof(sql), name->key);
         copy_data(sql);

         copy_data("\"=>\"");

         escape_array_record(sql, sizeof(sql), name->value);
         copy_data(sql);

         copy_data("\"");
      }
      copy_data("\t");
   }
   else
   {
      copy_data("\\N\t");
   }

   sprintf(sql, "%d\t", adminlevel);
   copy_data(sql);

   if (housenumber)
   {
      escape(sql, sizeof(sql), housenumber->value);
      copy_data(sql);
      copy_data("\t");
   }
   else
   {
      copy_data("\\N\t");
   }

   if (street)
   {
      escape(sql, sizeof(sql), street->value);
      copy_data(sql);
      copy_data("\t");
   }
   else
   {
      copy_data("\\N\t");
   }

   if (isin)
   {
      // Skip the leading ',' from the contactination
      escape(sql, sizeof(sql), isin+1);
      copy_data(sql);
      copy_data("\t");
   }
   else
   {
      copy_data("\\N\t");
   }

   if (postcode)
   {
      escape(sql, sizeof(sql), postcode->value);
      copy_data(sql);
      copy_data("\t");
   }
   else
   {
      copy_data("\\N\t");
   }

   if (countrycode)
   {
      escape(sql, sizeof(sql), countrycode->value);
      copy_data(sql);
      copy_data("\t");
   }
   else
   {
     copy_data("\\N\t");
   }

   /* extra tags array */
   if (listHasData(names))
   {
      first = 1;
      for (name = firstItem(extratags); name; name = nextItem(extratags, name))
      {
         if (first) first = 0;
         else copy_data(", ");

         copy_data("\"");

         escape_array_record(sql, sizeof(sql), name->key);
         copy_data(sql);

         copy_data("\"=>\"");

         escape_array_record(sql, sizeof(sql), name->value);
         copy_data(sql);

         copy_data("\"");
      }
      copy_data("\t");
   }
   else
   {
      copy_data("\\N\t");
   }

   sprintf(sql, "SRID=%d;", SRID);
   copy_data(sql);
   copy_data(wkt);

   copy_data("\n");

//fprintf(stderr, "%c %" PRIdOSMID " %s\n", osm_type, osm_id, wkt);

   return;
}

static void add_polygon_error(char osm_type, osmid_t osm_id, const char *class, const char *type, 
  struct keyval *names, const char *countrycode, const char *wkt)
{
   int first;
   struct keyval *name;
   char sql[2048];

   /* Output a copy line for this place */
   sprintf(sql, "%c\t%" PRIdOSMID "\t", osm_type, osm_id);
   copy_error_data(sql);

   escape(sql, sizeof(sql), class);
   copy_error_data(sql);
   copy_error_data("\t");

   escape(sql, sizeof(sql), type);
   copy_error_data(sql);
   copy_error_data("\t");

   /* start name array */
   if (listHasData(names))
   {
      first = 1;
      for (name = firstItem(names); name; name = nextItem(names, name))
      {
         if (first) first = 0;
         else copy_error_data(", ");

         copy_error_data("\"");

         escape_array_record(sql, sizeof(sql), name->key);
         copy_error_data(sql);

         copy_error_data("\"=>\"");

         escape_array_record(sql, sizeof(sql), name->value);
         copy_error_data(sql);

         copy_error_data("\"");
      }
      copy_error_data("\t");
   }
   else
   {
      copy_error_data("\\N\t");
   }

   if (countrycode)
   {
      escape(sql, sizeof(sql), countrycode);
      copy_error_data(sql);
      copy_error_data("\t");
   }
   else
   {
     copy_error_data("\\N\t");
   }

   copy_error_data("now\tNot a polygon\t\\N\t");

   sprintf(sql, "SRID=%d;", SRID);
   copy_error_data(sql);
   copy_error_data(wkt);

   copy_error_data("\n");

//fprintf(stderr, "%c %" PRIdOSMID " %s\n", osm_type, osm_id, wkt);

   return;
}


static void delete_place(char osm_type, osmid_t osm_id)
{
   /* Stop any active copy */
   stop_copy();

   /* Delete all places for this object */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "DELETE FROM place WHERE osm_type = '%c' AND osm_id = %" PRIdOSMID, osm_type, osm_id);

   return;
}

static int gazetteer_out_start(const struct output_options *options)
{
   /* Save option handle */
   Options = options;

   /* Connection to the database */
   Connection = PQconnectdb(options->conninfo);
   //ConnectionError = PQconnectdb(options->conninfo);

   /* Check to see that the backend connection was successfully made */
   if (PQstatus(Connection) != CONNECTION_OK)
   {
      fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(Connection));
      exit_nicely();
   }

   /* Start a transaction */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "BEGIN");

   /* (Re)create the table unless we are appending */
   if (!Options->append)
   {
      /* Drop any existing table */
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS place");
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TYPE if exists keyvalue cascade");
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TYPE if exists wordscore cascade");
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TYPE if exists stringlanguagetype cascade");
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TYPE if exists keyvaluetype cascade");
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP FUNCTION IF EXISTS get_connected_ways(integer[])");

      /* Create types and functions */
      pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_KEYVALUETYPE_TYPE, "", "");
      pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_WORDSCORE_TYPE, Options->tblsmain_data);

      /* Create the new table */
      if (Options->tblsmain_data)
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK,
                      CREATE_PLACE_TABLE, "TABLESPACE", Options->tblsmain_data);
      }
      else
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_TABLE, "", "");
      }
      if (Options->tblsmain_index)
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_ID_INDEX, "TABLESPACE", Options->tblsmain_index);
      }
      else
      {
          pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_ID_INDEX, "", "");
      }

      pgsql_exec(Connection, PGRES_TUPLES_OK, "SELECT AddGeometryColumn('place', 'geometry', %d, 'GEOMETRY', 2)", SRID);
      pgsql_exec(Connection, PGRES_COMMAND_OK, "ALTER TABLE place ALTER COLUMN geometry SET NOT NULL");
   } else {
      ConnectionDelete = PQconnectdb(options->conninfo);
      if (PQstatus(ConnectionDelete) != CONNECTION_OK)
      { 
          fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(ConnectionDelete));
          exit_nicely();
      }

      pgsql_exec(ConnectionDelete, PGRES_COMMAND_OK, "PREPARE get_classes (CHAR(1), " POSTGRES_OSMID_TYPE ") AS SELECT class FROM place WHERE osm_type = $1 and osm_id = $2");
   }

   /* Setup middle layer */
   options->mid->start(options);

   hLog = fopen("log", "w");

   return 0;
}

static void gazetteer_out_stop(void)
{
   /* Process any remaining ways and relations */
//   Options->mid->iterate_ways( gazetteer_out_way );
//   Options->mid->iterate_relations( gazetteer_process_relation );

   /* No longer need to access middle layer */
   Options->mid->commit();
   Options->mid->stop();

   /* Stop any active copy */
   stop_copy();
   //stop_error_copy();
   if (hLog) fclose(hLog);

   /* Commit transaction */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "COMMIT");

   /* Analyse the table */
   //pgsql_exec(Connection, PGRES_COMMAND_OK, "ANALYZE place");

   PQfinish(Connection);
   if (ConnectionDelete)
       PQfinish(ConnectionDelete);
   if (ConnectionError)
       PQfinish(ConnectionError);

   return;
}

static void gazetteer_out_cleanup(void)
{
   return;
}

static int gazetteer_process_node(osmid_t id, double lat, double lon, struct keyval *tags, int delete_old)
{
   struct keyval names;
   struct keyval places;
   struct keyval extratags;
   struct keyval *place;
   int adminlevel;
   struct keyval * housenumber;
   struct keyval * street;
   char * isin;
   struct keyval * postcode;
   struct keyval * countrycode;
   char wkt[128];

//fprintf(stderr, "node\n");

   /* Split the tags */
   split_tags(tags, TAGINFO_NODE, &names, &places, &extratags, &adminlevel, &housenumber, &street, &isin, &postcode, &countrycode);

   /* Feed this node to the middle layer */
   Options->mid->nodes_set(id, lat, lon, tags);

   if (delete_old)
       delete_unused_classes('N', id, &places);

   /* Are we interested in this item? */
   if (listHasData(&places))
   {
      sprintf(wkt, "POINT(%.15g %.15g)", lon, lat);
      for (place = firstItem(&places); place; place = nextItem(&places, place))
      {
         add_place('N', id, place->key, place->value, &names, &extratags, adminlevel, housenumber, street, isin, postcode, countrycode, wkt);
      }
   }

   if (housenumber) freeItem(housenumber);
   if (street) freeItem(street);
   if (isin) free(isin);
   if (postcode) freeItem(postcode);
   if (countrycode) freeItem(countrycode);

   /* Free tag lists */
   resetList(&names);
   resetList(&places);
   resetList(&extratags);

   return 0;
}

static int gazetteer_add_node(osmid_t id, double lat, double lon, struct keyval *tags)
{
    return gazetteer_process_node(id, lat, lon, tags, 0);
}

static int gazetteer_process_way(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags, int delete_old)
{
   struct keyval names;
   struct keyval places;
   struct keyval extratags;
   struct keyval *place;
   int adminlevel;
   struct keyval * housenumber;
   struct keyval * street;
   char * isin;
   struct keyval * postcode;
   struct keyval * countrycode;
   int area;

//fprintf(stderr, "way\n");

   /* Split the tags */
   area = split_tags(tags, TAGINFO_WAY, &names, &places, &extratags, &adminlevel, &housenumber, &street, &isin, &postcode, &countrycode);

   /* Feed this way to the middle layer */
   Options->mid->ways_set(id, ndv, ndc, tags, 0);

   if (delete_old)
       delete_unused_classes('W', id, &places);

   /* Are we interested in this item? */
   if (listHasData(&places))
   {
      struct osmNode *nodev;
      int nodec;
      char *wkt;
    
      /* Fetch the node details */
      nodev = malloc(ndc * sizeof(struct osmNode));
      nodec = Options->mid->nodes_get_list(nodev, ndv, ndc);

      /* Get the geometry of the object */
      if ((wkt = get_wkt_simple(nodev, nodec, area)) != NULL && strlen(wkt) > 0)
      {
         for (place = firstItem(&places); place; place = nextItem(&places, place))
         {
            add_place('W', id, place->key, place->value, &names, &extratags, adminlevel, housenumber, street, isin, postcode, countrycode, wkt);
         }
      }

      /* Free the geometry */
      free(wkt);

      /* Free the nodes */
      free(nodev);
   }

   if (housenumber) freeItem(housenumber);
   if (street) freeItem(street);
   if (isin) free(isin);
   if (postcode) freeItem(postcode);
   if (countrycode) freeItem(countrycode);

   /* Free tag lists */
   resetList(&names);
   resetList(&places);
   resetList(&extratags);

   return 0;
}

static int gazetteer_add_way(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags)
{
    return gazetteer_process_way(id, ndv, ndc, tags, 0);
}

static int gazetteer_process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags, int delete_old)
{
   struct keyval names;
   struct keyval places;
   struct keyval extratags;
   struct keyval *place;
   int adminlevel;
   struct keyval * housenumber;
   struct keyval * street;
   char * isin;
   struct keyval * postcode;
   struct keyval * countrycode;
   int area, wkt_size;
   const char *type;

   type = getItem(tags, "type");
   if (!type) {
      if (delete_old) delete_unused_classes('R', id, 0); 
      return 0;
   }

   if (!strcmp(type, "associatedStreet") || !strcmp(type, "relatedStreet"))
   {
      Options->mid->relations_set(id, members, member_count, tags);
      if (delete_old) delete_unused_classes('R', id, 0); 
      return 0;
   }

   if (strcmp(type, "boundary") && strcmp(type, "multipolygon")) {
      if (delete_old) delete_unused_classes('R', id, 0); 
      return 0;
   }

   Options->mid->relations_set(id, members, member_count, tags);

   /* Split the tags */
   area = split_tags(tags, TAGINFO_AREA, &names, &places, &extratags, &adminlevel, &housenumber, &street, &isin, &postcode, &countrycode);

   if (delete_old)
       delete_unused_classes('R', id, &places);

   if (listHasData(&places))
   {
      /* get the boundary path (ways) */
      int i, count;
      int *xcount = malloc( (member_count+1) * sizeof(int) );
      struct keyval *xtags  = malloc( (member_count+1) * sizeof(struct keyval) );
      struct osmNode **xnodes = malloc( (member_count+1) * sizeof(struct osmNode*) );

      count = 0;
      for (i=0; i<member_count; i++)
      {
         /* only interested in ways */
         if (members[i].type != OSMTYPE_WAY)
            continue;

         initList(&(xtags[count]));
         if (Options->mid->ways_get( members[i].id, &(xtags[count]), &(xnodes[count]), &(xcount[count])))
            continue;
         count++;
      }
      xnodes[count] = NULL;
      xcount[count] = 0;

      wkt_size = build_geometry(id, xnodes, xcount, 1, 1, 1000000);
      for (i=0;i<wkt_size;i++)
      {
         char *wkt = get_wkt(i);
         if (strlen(wkt) && (!strncmp(wkt, "POLYGON", strlen("POLYGON")) || !strncmp(wkt, "MULTIPOLYGON", strlen("MULTIPOLYGON"))))
         {
             for (place = firstItem(&places); place; place = nextItem(&places, place))
             {
                add_place('R', id, place->key, place->value, &names, &extratags, adminlevel, housenumber, street, isin, postcode, countrycode, wkt);
             }
         }
         else
         {
            //add_polygon_error('R', id, "boundary", "adminitrative", &names, countrycode, wkt);
         }
         free(wkt);
      }
      clear_wkts();

      for( i=0; i<count; i++ )
      {
         resetList( &(xtags[i]) );
         free( xnodes[i] );
      }

      free(xcount);
      free(xtags);
      free(xnodes);
   }

   if (housenumber) freeItem(housenumber);
   if (street) freeItem(street);
   if (isin) free(isin);
   if (postcode) freeItem(postcode);
   if (countrycode) freeItem(countrycode);

   /* Free tag lists */
   resetList(&names);
   resetList(&places);
   resetList(&extratags);

   return 0;
}

static int gazetteer_add_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags) 
{
    return gazetteer_process_relation(id, members, member_count, tags, 0);
}

static int gazetteer_delete_node(osmid_t id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this node */
   delete_place('N', id);

   /* Feed this delete to the middle layer */
   Options->mid->nodes_delete(id);

   return 0;
}

static int gazetteer_delete_way(osmid_t id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this way */
   delete_place('W', id);

   /* Feed this delete to the middle layer */
   Options->mid->ways_delete(id);

   return 0;
}

static int gazetteer_delete_relation(osmid_t id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this relation */
   delete_place('R', id);

   /* Feed this delete to the middle layer */
   Options->mid->relations_delete(id);

   return 0;
}

static int gazetteer_modify_node(osmid_t id, double lat, double lon, struct keyval *tags)
{
   require_slim_mode();
   Options->mid->nodes_delete(id);
   return gazetteer_process_node(id, lat, lon, tags, 1);
}

static int gazetteer_modify_way(osmid_t id, osmid_t *ndv, int ndc, struct keyval *tags)
{
   require_slim_mode();
   Options->mid->ways_delete(id);
   return gazetteer_process_way(id, ndv, ndc, tags, 1);
}

static int gazetteer_modify_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
   require_slim_mode();
   Options->mid->relations_delete(id);
   return gazetteer_process_relation(id, members, member_count, tags, 1);
}

struct output_t out_gazetteer = {
   .start = gazetteer_out_start,
   .stop = gazetteer_out_stop,
   .cleanup = gazetteer_out_cleanup,

   .node_add = gazetteer_add_node,
   .way_add = gazetteer_add_way,
   .relation_add = gazetteer_add_relation,

   .node_modify = gazetteer_modify_node,
   .way_modify = gazetteer_modify_way,
   .relation_modify = gazetteer_modify_relation,

   .node_delete = gazetteer_delete_node,
   .way_delete = gazetteer_delete_way,
   .relation_delete = gazetteer_delete_relation
};
