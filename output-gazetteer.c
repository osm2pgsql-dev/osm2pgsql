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

#define CREATE_PLACE_TABLE                      \
   "CREATE TABLE place ("                       \
   "  osm_type CHAR(1) NOT NULL,"               \
   "  osm_id BIGINT NOT NULL,"                  \
   "  class TEXT NOT NULL,"                     \
   "  type TEXT NOT NULL,"                      \
   "  name TEXT NOT NULL,"                      \
   "  rank INTEGER NOT NULL"                    \
   ")"

#define CREATE_PLACE_ID_INDEX \
   "CREATE INDEX place_id_idx ON place USING BTREE (osm_type, osm_id)"

#define CREATE_PLACE_TYPE_INDEX \
   "CREATE INDEX place_type_idx ON place USING BTREE (class, type)"

#define CREATE_PLACE_NAME_INDEX \
   "CREATE INDEX place_name_idx ON place USING GIN (TO_TSVECTOR('simple', name))"

#define CREATE_PLACE_GEOMETRY_INDEX \
   "CREATE INDEX place_geometry_idx ON place USING GIST (geometry)"

#define TAGINFO_NODE 0x1u
#define TAGINFO_WAY  0x2u
#define TAGINFO_AREA 0x4u

static const struct taginfo {
   const char   *name;
   const char   *value;
   unsigned int flags;
} taginfo[] = {
   { "amenity",  NULL,      TAGINFO_NODE|TAGINFO_WAY|TAGINFO_AREA },
   { "highway",  NULL,      TAGINFO_WAY                           },
   { "historic", NULL,      TAGINFO_NODE|TAGINFO_WAY|TAGINFO_AREA },
   { "place",    NULL,      TAGINFO_NODE                          },
   { "railway",  "station", TAGINFO_NODE                          },
   { "railway",  "halt",    TAGINFO_NODE                          },
   { "tourism",  NULL,      TAGINFO_NODE|TAGINFO_WAY|TAGINFO_AREA },
   { "waterway", NULL,      TAGINFO_WAY                           },
   { NULL,       NULL,      0                                     }
};

static const struct output_options *Options = NULL;
static PGconn *Connection = NULL;
static int CopyActive = 0;
static char Buffer[BUFFER_SIZE];
static unsigned int BufferLen = 0;

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

static int split_tags(struct keyval *tags, unsigned int flags, struct keyval *names, struct keyval *places)
{
   int area = 0;
   struct keyval *item;

   /* Initialise the result lists */
   initList(names);
   initList(places);

   /* Loop over the tags */
   while ((item = popItem(tags)) != NULL)
   {
      /* If this is a name tag, add it to the name list */
      if (strcmp(item->key, "ref") == 0 ||
          strcmp(item->key, "iata") == 0 ||
          strcmp(item->key, "icao") == 0 ||
          strcmp(item->key, "name") == 0 ||
          strcmp(item->key, "old_name") == 0 ||
          strcmp(item->key, "loc_name") == 0 ||
          strcmp(item->key, "alt_name") == 0 ||
          strncmp(item->key, "name:", 5) == 0)
      {
         pushItem(names, item);
      }
      else
      {
         const struct taginfo *t;

         /* If this is a tag we want then add it to the place list */
         for (t = taginfo; t->name != NULL; t++)
         {
            if ((t->flags & flags) != 0)
            {
               if (strcmp(t->name, item->key) == 0 &&
                   (t->value == NULL || strcmp(t->value, item->value) == 0))
               {
                  if ((t->flags & TAGINFO_AREA) != 0) area = 1;

                  pushItem(places, item);

                  break;
               }
            }
         }

         /* Free the tag if we didn't want it */
         if (t->name == NULL) freeItem(item);
      }
   }

   return area;
}

static void add_place(char osm_type, int osm_id, const char *class, const char *type, const char *name, int rank, const char *wkt)
{
   char sql[2048];

   /* Output a copy line for this place */
   sprintf(sql, "%c\t%d\t", osm_type, osm_id);
   copy_data(sql);
   escape(sql, sizeof(sql), class);
   copy_data(sql);
   copy_data("\t");
   escape(sql, sizeof(sql), type);
   copy_data(sql);
   copy_data("\t");
   escape(sql, sizeof(sql), name);
   copy_data(sql);
   sprintf(sql, "\t%d\tSRID=%d;", rank, SRID);
   copy_data(sql);
   copy_data(wkt);
   copy_data("\n");

   return;
}

static void delete_place(char osm_type, int osm_id)
{
   /* Stop any active copy */
   stop_copy();

   /* Delete all places for this object */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "DELETE FROM place WHERE osm_type = '%c' AND osm_id = %d", osm_type, osm_id);

   return;
}

static int gazetteer_out_start(const struct output_options *options)
{
   /* Save option handle */
   Options = options;

   /* Connection to the database */
   Connection = PQconnectdb(options->conninfo);

   /* Check to see that the backend connection was successfully made */
   if (PQstatus(Connection) != CONNECTION_OK)
   {
      fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(Connection));
      exit_nicely();
   }

   /* Start a transaction */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "BEGIN");

   /* (Re)create the table unless we are appending */
   if (!options->append)
   {
      /* Drop any existing table */
      pgsql_exec(Connection, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS place");

      /* Create the new table */
      pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_TABLE);
      pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_ID_INDEX);
      pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_TYPE_INDEX);
      pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_NAME_INDEX);
      pgsql_exec(Connection, PGRES_TUPLES_OK, "SELECT AddGeometryColumn('place', 'geometry', %d, 'GEOMETRY', 2)", SRID);
      pgsql_exec(Connection, PGRES_COMMAND_OK, "ALTER TABLE place ALTER COLUMN geometry SET NOT NULL");
      pgsql_exec(Connection, PGRES_COMMAND_OK, CREATE_PLACE_GEOMETRY_INDEX);
   }

   /* Setup middle layer */
   options->mid->start(options);

   return 0;
}

static void gazetteer_out_stop(void)
{
   /* Process any remaining ways and relations */
//   Options->mid->iterate_ways( gazetteer_out_way );
//   Options->mid->iterate_relations( gazetteer_process_relation );

   /* No longer need to access middle layer */
   Options->mid->stop();

   /* Stop any active copy */
   stop_copy();

   /* Commit transaction */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "COMMIT");

   /* Analyse the table */
   pgsql_exec(Connection, PGRES_COMMAND_OK, "ANALYZE place");

   return;
}

static void gazetteer_out_cleanup(void)
{
   return;
}

static int gazetteer_add_node(int id, double lat, double lon, struct keyval *tags)
{
   struct keyval names;
   struct keyval places;

   /* Split the tags */
   split_tags(tags, TAGINFO_NODE, &names, &places);

   /* Feed this node to the middle layer */
   Options->mid->nodes_set(id, lat, lon, tags);

   /* Are we interested in this item? */
   if (listHasData(&names) && listHasData(&places))
   {
      struct keyval *name;
      struct keyval *place;

      for (name = firstItem(&names); name; name = nextItem(&names, name))
      {
         for (place = firstItem(&places); place; place = nextItem(&places, place))
         {
            int  rank = 0;
            char wkt[128];

            if (strcasecmp(place->key, "place") == 0)
            {
               if (strcasecmp(place->value, "city") == 0) rank = 5;
               else if (strcasecmp(place->value, "town") == 0) rank = 4;
               else if (strcasecmp(place->value, "village") == 0) rank = 3;
               else if (strcasecmp(place->value, "hamlet") == 0) rank = 2;
               else if (strcasecmp(place->value, "suburb") == 0) rank = 1;
            }

            sprintf(wkt, "POINT(%.15g %.15g)", lon, lat);

            add_place('N', id, place->key, place->value, name->value, rank, wkt);
         }
      }
   }

   /* Free tag lists */
   resetList(&names);
   resetList(&places);

   return 0;
}

static int gazetteer_add_way(int id, int *ndv, int ndc, struct keyval *tags)
{
   struct keyval names;
   struct keyval places;
   int area;

   /* Split the tags */
   area = split_tags(tags, TAGINFO_WAY, &names, &places);

   /* Are we interested in this item? */
   if (listHasData(&names) && listHasData(&places))
   {
      struct osmNode *nodev;
      int nodec;
      char *wkt;
      double dummy;

      /* Feed this way to the middle layer */
      Options->mid->ways_set(id, ndv, ndc, tags, 0);

      /* Fetch the node details */
      nodev = malloc(ndc * sizeof(struct osmNode));
      nodec = Options->mid->nodes_get_list(nodev, ndv, ndc);

      /* Get the geometry of the object */
      if ((wkt = get_wkt_simple(nodev, nodec, area, &dummy, &dummy, &dummy)) != NULL && strlen(wkt) > 0)
      {
         struct keyval *name;
         struct keyval *place;

         for (name = firstItem(&names); name; name = nextItem(&names, name))
         {
            for (place = firstItem(&places); place; place = nextItem(&places, place))
            {
               add_place('W', id, place->key, place->value, name->value, 0, wkt);
            }
         }
      }

      /* Free the geometry */
      free(wkt);

      /* Free the nodes */
      free(nodev);
   }

   /* Free tag lists */
   resetList(&names);
   resetList(&places);

   return 0;
}

static int gazetteer_add_relation(int id, struct member *membv, int membc, struct keyval *tags)
{
   return 0;
}

static int gazetteer_delete_node(int id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this node */
   delete_place('N', id);

   /* Feed this delete to the middle layer */
   Options->mid->nodes_delete(id);

   return 0;
}

static int gazetteer_delete_way(int id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this way */
   delete_place('W', id);

   /* Feed this delete to the middle layer */
   Options->mid->ways_delete(id);

   return 0;
}

static int gazetteer_delete_relation(int id)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Feed this delete to the middle layer */
   Options->mid->relations_delete(id);

   return 0;
}

static int gazetteer_modify_node(int id, double lat, double lon, struct keyval *tags)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this node... */
   gazetteer_delete_node(id);

   /* ...then add it back again */
   gazetteer_add_node(id, lat, lon, tags);

   /* Feed this change to the middle layer */
   Options->mid->node_changed(id);

   return 0;
}

static int gazetteer_modify_way(int id, int *ndv, int ndc, struct keyval *tags)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Delete all references to this way... */
   gazetteer_delete_way(id);

   /* ...then add it back again */
   gazetteer_add_way(id, ndv, ndc, tags);

   /* Feed this change to the middle layer */
   Options->mid->way_changed(id);

   return 0;
}

static int gazetteer_modify_relation(int id, struct member *membv, int membc, struct keyval *tags)
{
   /* Make sure we are in slim mode */
   require_slim_mode();

   /* Feed this change to the middle layer */
   Options->mid->relation_changed(id);

   return 0;
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
