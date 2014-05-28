/* Implements the mid-layer processing for osm2pgsql
 * using several PostgreSQL tables
 * 
 * This layer stores data read in from the planet.osm file
 * and is then read by the backend processing code to
 * emit the final geometry-enabled output formats
*/

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stdexcept>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <libpq-fe.h>

#include "osmtypes.hpp"
#include "output.hpp"
#include "reprojection.hpp"
#include "output-pgsql.hpp"
#include "build_geometry.hpp"
#include "middle.hpp"
#include "pgsql.hpp"
#include "expire-tiles.hpp"
#include "wildcmp.hpp"
#include "node-ram-cache.hpp"
#include "taginfo_impl.hpp"
#include "tagtransform.hpp"
#include "buffer.hpp"
#include "util.hpp"

#include <boost/bind.hpp>
#include <iostream>
#include <limits>

#define SRID (reproj->project_getprojinfo()->srs)

/* FIXME: Shouldn't malloc this all to begin with but call realloc()
   as required. The program will most likely segfault if it reads a
   style file with more styles than this */
#define MAX_STYLES 1000

#define NUM_TABLES (output_pgsql_t::t_MAX)

output_pgsql_t::table::table(const char *name_, const char *type_)
    : name(strdup(name_)), type(type_),
      sql_conn(NULL), buflen(0), copyMode(0),
      columns(NULL)
{
    memset(buffer, 0, sizeof buffer);
}

/* NOTE: section below for flags genuinely is static and
 * constant, so there's no need to hoist this into a per
 * class variable. It doesn't get modified, so it's safe
 * to share across threads and its lifetime is the whole
 * program.
 */
struct flagsname {
    flagsname(const char *name_, int flag_)
        : name(strdup(name_)), flag(flag_) {
    }
    char *name;
    int flag;
};

static const flagsname tagflags[] = {
    flagsname("polygon", FLAG_POLYGON),
    flagsname("linear",  FLAG_LINEAR),
    flagsname("nocache", FLAG_NOCACHE),
    flagsname("delete",  FLAG_DELETE),
    flagsname("phstore", FLAG_PHSTORE)
};
#define NUM_FLAGS ((signed)(sizeof(tagflags) / sizeof(tagflags[0])))

int read_style_file( const char *filename, export_list *exlist )
{
  FILE *in;
  int lineno = 0;
  int num_read = 0;
  char osmtype[24];
  char tag[64];
  char datatype[24];
  char flags[128];
  int i;
  char *str;
  int fields;
  struct taginfo temp;
  char buffer[1024];
  int enable_way_area = 1;

  in = fopen( filename, "rt" );
  if( !in )
  {
    fprintf( stderr, "Couldn't open style file '%s': %s\n", filename, strerror(errno) );
    exit_nicely();
  }
  
  //for each line of the style file
  while( fgets( buffer, sizeof(buffer), in) != NULL )
  {
    lineno++;
    
    //find where a comment starts and terminate the string there
    str = strchr( buffer, '#' );
    if( str )
      *str = '\0';

    //grab the expected fields for this row
    fields = sscanf( buffer, "%23s %63s %23s %127s", osmtype, tag, datatype, flags );
    if( fields <= 0 )  /* Blank line */
      continue;
    if( fields < 3 )
    {
      fprintf( stderr, "Error reading style file line %d (fields=%d)\n", lineno, fields );
      exit_nicely();
    }

    //place to keep info about this tag
    temp.name.assign(tag);
    temp.type.assign(datatype);
    temp.flags = 0;
    temp.count = 0;
    
    //split the flags column on commas and keep track of which flags you've seen in a bit mask
    for( str = strtok( flags, ",\r\n" ); str; str = strtok(NULL, ",\r\n") )
    {
      for( i=0; i<NUM_FLAGS; i++ )
      {
        if( strcmp( tagflags[i].name, str ) == 0 )
        {
          temp.flags |= tagflags[i].flag;
          break;
        }
      }
      if( i == NUM_FLAGS )
        fprintf( stderr, "Unknown flag '%s' line %d, ignored\n", str, lineno );
    }

    if ((temp.flags != FLAG_DELETE) && 
        ((temp.name.find('?') != std::string::npos) || 
         (temp.name.find('*') != std::string::npos))) {
        fprintf( stderr, "wildcard '%s' in non-delete style entry\n",temp.name.c_str());
        exit_nicely();
    }
    
    if ((temp.name == "way_area") && (temp.flags==FLAG_DELETE)) {
        enable_way_area=0;
    }

    /*    printf("%s %s %d %d\n", temp.name, temp.type, temp.polygon, offset ); */
    bool kept = false;
    
    //keep this tag info if it applies to nodes
    if( strstr( osmtype, "node" ) )
    {
        exlist->add(OSMTYPE_NODE, temp);
        kept = true;
    }

    //keep this tag info if it applies to ways
    if( strstr( osmtype, "way" ) )
    {
        exlist->add(OSMTYPE_WAY, temp);
        kept = true;
    }

    //do we really want to completely quit on an unusable line?
    if( !kept )
    {
      fprintf( stderr, "Weird style line %d\n", lineno );
      exit_nicely();
    }
    num_read++;
  }


  if (ferror(in)) {
      perror(filename);
      exit_nicely();
  }
  if (num_read == 0) {
      fprintf(stderr, "Unable to parse any valid columns from the style file. Aborting.\n");
      exit_nicely();
  }
  fclose(in);
  return enable_way_area;
}

/* Handles copying out, but coalesces the data into large chunks for
 * efficiency. PostgreSQL doesn't actually need this, but each time you send
 * a block of data you get 5 bytes of overhead. Since we go column by column
 * with most empty and one byte delimiters, without this optimisation we
 * transfer three times the amount of data necessary.
 */
void output_pgsql_t::copy_to_table(enum table_id table, const char *sql)
{
    PGconn *sql_conn = m_tables[table].sql_conn;
    unsigned int len = strlen(sql);
    unsigned int buflen = m_tables[table].buflen;
    char *buffer = m_tables[table].buffer;

    /* Return to copy mode if we dropped out */
    if( !m_tables[table].copyMode )
    {
        pgsql_exec(sql_conn, PGRES_COPY_IN, "COPY %s (%s,way) FROM STDIN", m_tables[table].name, m_tables[table].columns);
        m_tables[table].copyMode = 1;
    }
    /* If the combination of old and new data is too big, flush old data */
    if( (unsigned)(buflen + len) > sizeof( m_tables[table].buffer )-10 )
    {
      pgsql_CopyData(m_tables[table].name, sql_conn, buffer);
      buflen = 0;

      /* If new data by itself is also too big, output it immediately */
      if( (unsigned)len > sizeof( m_tables[table].buffer )-10 )
      {
        pgsql_CopyData(m_tables[table].name, sql_conn, sql);
        len = 0;
      }
    }
    /* Normal case, just append to buffer */
    if( len > 0 )
    {
      strcpy( buffer+buflen, sql );
      buflen += len;
      len = 0;
    }

    /* If we have completed a line, output it */
    if( buflen > 0 && buffer[buflen-1] == '\n' )
    {
      pgsql_CopyData(m_tables[table].name, sql_conn, buffer);
      buflen = 0;
    }

    m_tables[table].buflen = buflen;
}




void output_pgsql_t::cleanup(void)
{
    int i;

    for (i=0; i<NUM_TABLES; i++) {
        if (m_tables[i].sql_conn) {
            PQfinish(m_tables[i].sql_conn);
            m_tables[i].sql_conn = NULL;
        }
    }
}

/* Escape data appropriate to the type */
static void escape_type(buffer &sql, const char *value, const char *type) {
  int items;

  if ( !strcmp(type, "int4") ) {
    int from, to; 
    /* For integers we take the first number, or the average if it's a-b */
    items = sscanf(value, "%d-%d", &from, &to);
    if ( items == 1 ) {
      sql.printf("%d", from);
    } else if ( items == 2 ) {
      sql.printf("%d", (from + to) / 2);
    } else {
      sql.printf("\\N");
    }
  } else {
    /*
    try to "repair" real values as follows:
      * assume "," to be a decimal mark which need to be replaced by "."
      * like int4 take the first number, or the average if it's a-b
      * assume SI unit (meters)
      * convert feet to meters (1 foot = 0.3048 meters)
      * reject anything else    
    */
    if ( !strcmp(type, "real") ) {
      int i,slen;
      float from,to;

      // we're just using sql as a temporary buffer here.
      sql.cpy(value);

      slen=sql.len();
      for (i=0;i<slen;i++) if (sql.buf[i]==',') sql.buf[i]='.';

      items = sscanf(sql.buf, "%f-%f", &from, &to);
      if ( items == 1 ) {
	if ((sql.buf[slen-2]=='f') && (sql.buf[slen-1]=='t')) {
	  from*=0.3048;
	}
	sql.printf("%f", from);
      } else if ( items == 2 ) {
	if ((sql.buf[slen-2]=='f') && (sql.buf[slen-1]=='t')) {
	  from*=0.3048;
	  to*=0.3048;
	}
	sql.printf("%f", (from + to) / 2);
      } else {
	sql.printf("\\N");
      }
    } else {
      escape(sql, value);
    }
  }
}

void output_pgsql_t::write_hstore(enum output_pgsql_t::table_id table, struct keyval *tags,
                                  buffer &sql)
{
    size_t hlen;
    /* a clone of the tags pointer */
    struct keyval *xtags = tags;
        
    /* while this tags has a follow-up.. */
    while (xtags->next->key != NULL)
    {

      /* hard exclude z_order tag and keys which have their own column */
      if ((xtags->next->has_column) || (strcmp("z_order",xtags->next->key)==0)) {
          /* update the tag-pointer to point to the next tag */
          xtags = xtags->next;
          continue;
      }

      /*
        hstore ASCII representation looks like
        "<key>"=>"<value>"
        
        we need at least strlen(key)+strlen(value)+6+'\0' bytes
        in theory any single character could also be escaped
        thus we need an additional factor of 2.
        The maximum lenght of a single hstore element is thus
        calcuated as follows:
      */
      hlen=2 * (strlen(xtags->next->key) + strlen(xtags->next->value)) + 7;
      
      /* if the sql buffer is too small */
      if (hlen > sql.capacity()) {
        sql.reserve(hlen);
      }
        
      /* pack the tag with its value into the hstore */
      keyval2hstore(sql, xtags->next);
      copy_to_table(table, sql.buf);

      /* update the tag-pointer to point to the next tag */
      xtags = xtags->next;
        
      /* if the tag has a follow up, add a comma to the end */
      if (xtags->next->key != NULL)
          copy_to_table(table, ",");
    }
    
    /* finish the hstore column by placing a TAB into the data stream */
    copy_to_table(table, "\t");
    
    /* the main hstore-column has now been written */
}

/* write an hstore column to the database */
void output_pgsql_t::write_hstore_columns(enum table_id table, struct keyval *tags,
                                          buffer &sql)
{
    char *shortkey;
    /* the index of the current hstore column */
    int i_hstore_column;
    int found;
    struct keyval *xtags;
    char *pos;
    size_t hlen;
    
    /* iterate over all configured hstore colums in the options */
    for(i_hstore_column = 0; i_hstore_column < m_options->n_hstore_columns; i_hstore_column++)
    {
        /* did this node have a tag that matched the current hstore column */
        found = 0;
        
        /* a clone of the tags pointer */
        xtags = tags;
        
        /* while this tags has a follow-up.. */
        while (xtags->next->key != NULL) {
            
            /* check if the tag's key starts with the name of the hstore column */
            pos = strstr(xtags->next->key, m_options->hstore_columns[i_hstore_column]);
            
            /* and if it does.. */
            if(pos == xtags->next->key)
            {
                /* remember we found one */
                found=1;
                
                /* generate the short key name */
                shortkey = xtags->next->key + strlen(m_options->hstore_columns[i_hstore_column]);
                
                /* calculate the size needed for this hstore entry */
                hlen=2*(strlen(shortkey)+strlen(xtags->next->value))+7;
                
                /* if the sql buffer is too small */
                if (hlen > sql.capacity()) {
                    /* resize it */
                    sql.reserve(hlen);
                }
                
                /* and pack the shortkey with its value into the hstore */
                keyval2hstore_manual(sql, shortkey, xtags->next->value);
                copy_to_table(table, sql.buf);
                
                /* update the tag-pointer to point to the next tag */
                xtags=xtags->next;
                
                /* if the tag has a follow up, add a comma to the end */
                if (xtags->next->key != NULL)
                    copy_to_table(table, ",");
            }
            else
            {
                /* update the tag-pointer to point to the next tag */
                xtags=xtags->next;
            }
        }
        
        /* if no matching tag has been found, write a NULL */
        if(!found)
            copy_to_table(table, "\\N");
        
        /* finish the hstore column by placing a TAB into the data stream */
        copy_to_table(table, "\t");
    }
    
    /* all hstore-columns have now been written */
}

void output_pgsql_t::export_tags(enum table_id table, enum OsmType info_table,
                                 struct keyval *tags, buffer &sql) {
    std::vector<taginfo> &infos = m_export_list->get(info_table);
    for (int i=0; i < infos.size(); i++) {
        taginfo &info = infos[i];
        if (info.flags & FLAG_DELETE)
            continue;
        if ((info.flags & FLAG_PHSTORE) == FLAG_PHSTORE)
            continue;
        struct keyval *tag = NULL;
        if ((tag = getTag(tags, info.name.c_str())))
        {
            escape_type(sql, tag->value, info.type.c_str());
            info.count++;
            if (HSTORE_NORM==m_options->enable_hstore)
                tag->has_column=1;
        }
        else
            sql.printf("\\N");

        copy_to_table(table, sql.buf);
        copy_to_table(table, "\t");
    }
}

/* example from: pg_dump -F p -t planet_osm gis
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, building, bridge, layer, way) FROM stdin;
17959841        \N      \N      \N      \N      \N      \N      \N      bus_stop        \N      \N      \N      \N      \N      \N    -\N      0101000020E610000030CCA462B6C3D4BF92998C9B38E04940
17401934        The Horn        \N      \N      \N      \N      \N      \N      \N      \N      pub     \N      \N      \N      \N    -\N      0101000020E6100000C12FC937140FD5BFB4D2F4FB0CE04940
...

mine - 01 01000000 48424298424242424242424256427364
psql - 01 01000020 E6100000 30CCA462B6C3D4BF92998C9B38E04940
       01 01000020 E6100000 48424298424242424242424256427364
0x2000_0000 = hasSRID, following 4 bytes = srid, not supported by geos WKBWriter
Workaround - output SRID=4326;<WKB>
*/

int output_pgsql_t::pgsql_out_node(osmid_t id, struct keyval *tags, double node_lat, double node_lon,
                                   buffer &sql)
{

    int filter = m_tagtransform->filter_node_tags(tags, m_export_list);
    int i;
    struct keyval *tag;

    if (filter) return 1;

    expire->from_bbox(node_lon, node_lat, node_lon, node_lat);
    sql.printf("%" PRIdOSMID "\t", id);
    copy_to_table(t_point, sql.buf);

    export_tags(t_point, OSMTYPE_NODE, tags, sql);
    
    /* hstore columns */
    write_hstore_columns(t_point, tags, sql);
    
    /* check if a regular hstore is requested */
    if (m_options->enable_hstore)
        write_hstore(t_point, tags, sql);
    
#ifdef FIXED_POINT
    // guarantee that we use the same values as in the node cache
    node_lon = util::fix_to_double(util::double_to_fix(node_lon, m_options->scale), m_options->scale);
    node_lat = util::fix_to_double(util::double_to_fix(node_lat, m_options->scale), m_options->scale);
#endif

    sql.printf("SRID=%d;POINT(%.15g %.15g)", SRID, node_lon, node_lat);
    copy_to_table(t_point, sql.buf);
    copy_to_table(t_point, "\n");

    return 0;
}



void output_pgsql_t::write_wkts(osmid_t id, struct keyval *tags, const char *wkt, enum table_id table,
                                buffer &sql)
{
    int j;
    struct keyval *tag;

    sql.printf("%" PRIdOSMID "\t", id);
    copy_to_table(table, sql.buf);

    export_tags(table, OSMTYPE_WAY, tags, sql);

    /* hstore columns */
    write_hstore_columns(table, tags, sql);
    
    /* check if a regular hstore is requested */
    if (m_options->enable_hstore)
        write_hstore(table, tags, sql);

    sql.printf("SRID=%d;", SRID);
    copy_to_table(table, sql.buf);
    copy_to_table(table, wkt);
    copy_to_table(table, "\n");
}

/*static int tag_indicates_polygon(enum OsmType type, const char *key)
{
    int i;

    if (!strcmp(key, "area"))
        return 1;

    for (i=0; i < exportListCount[type]; i++) {
        if( strcmp( exportList[type][i].name, key ) == 0 )
            return exportList[type][i].flags & FLAG_POLYGON;
    }

    return 0;
}*/



/*
COPY planet_osm (osm_id, name, place, landuse, leisure, "natural", man_made, waterway, highway, railway, amenity, tourism, learning, bu
ilding, bridge, layer, way) FROM stdin;
198497  Bedford Road    \N      \N      \N      \N      \N      \N      residential     \N      \N      \N      \N      \N      \N    \N       0102000020E610000004000000452BF702B342D5BF1C60E63BF8DF49406B9C4D470037D5BF5471E316F3DF4940DFA815A6EF35D5BF9AE95E27F5DF4940B41EB
E4C1421D5BF24D06053E7DF4940
212696  Oswald Road     \N      \N      \N      \N      \N      \N      minor   \N      \N      \N      \N      \N      \N      \N    0102000020E610000004000000467D923B6C22D5BFA359D93EE4DF4940B3976DA7AD11D5BF84BBB376DBDF4940997FF44D9A06D5BF4223D8B8FEDF49404D158C4AEA04D
5BF5BB39597FCDF4940
*/
int output_pgsql_t::pgsql_out_way(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists, buffer &sql)
{
    int polygon = 0, roads = 0;
    int i, wkt_size;
    double split_at;
    double area;

    /* If the flag says this object may exist already, delete it first */
    if(exists) {
        pgsql_delete_way_from_output(id);
        // TODO: this now only has an effect when called from the iterate_ways
        // call-back, so we need some alternative way to trigger this within
        // osmdata_t.
        slim_middle_t *slim = dynamic_cast<slim_middle_t *>(m_mid);
        const std::vector<osmid_t> rel_ids = slim->relations_using_way(id);
        for (std::vector<osmid_t>::const_iterator itr = rel_ids.begin();
             itr != rel_ids.end(); ++itr) {
            rels_pending_tracker->mark(*itr);
        }
    }

    if (m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list))
        return 0;
    /* Split long ways after around 1 degree or 100km */
    if (m_options->projection->get_proj_id() == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    wkt_size = builder.get_wkt_split(nodes, count, polygon, split_at);

    for (i=0;i<wkt_size;i++)
    {
        char *wkt = builder.get_wkt(i);

        if (wkt && strlen(wkt)) {
            /* FIXME: there should be a better way to detect polygons */
            if (!strncmp(wkt, "POLYGON", strlen("POLYGON")) || !strncmp(wkt, "MULTIPOLYGON", strlen("MULTIPOLYGON"))) {
                expire->from_nodes_poly(nodes, count, id);
                area = builder.get_area(i);
                if ((area > 0.0) && m_enable_way_area) {
                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "%g", area);
                    addItem(tags, "way_area", tmp, 0);
                }
                write_wkts(id, tags, wkt, t_poly, sql);
            } else {
                expire->from_nodes_line(nodes, count);
                write_wkts(id, tags, wkt, t_line, sql);
                if (roads)
                    write_wkts(id, tags, wkt, t_roads, sql);
            }
        }
        free(wkt);
    }
    builder.clear_wkts();
	
    return 0;
}

int output_pgsql_t::pgsql_out_relation(osmid_t id, struct keyval *rel_tags, int member_count, struct osmNode **xnodes, struct keyval *xtags, int *xcount, osmid_t *xid, const char **xrole, buffer &sql)
{
    int i, wkt_size;
    int roads = 0;
    int make_polygon = 0;
    int make_boundary = 0;
    int * members_superseeded;
    double split_at;

    members_superseeded = (int *)calloc(sizeof(int), member_count);

    if (member_count == 0) {
        free(members_superseeded);
        return 0;
    }

    if (m_tagtransform->filter_rel_member_tags(rel_tags, member_count, xtags, xrole, members_superseeded, &make_boundary, &make_polygon, &roads, m_export_list)) {
        free(members_superseeded);
        return 0;
    }
    
    /* Split long linear ways after around 1 degree or 100km (polygons not effected) */
    if (m_options->projection->get_proj_id() == PROJ_LATLONG)
        split_at = 1;
    else
        split_at = 100 * 1000;

    wkt_size = builder.build(id, xnodes, xcount, make_polygon, m_options->enable_multi, split_at);

    if (!wkt_size) {
        free(members_superseeded);
        return 0;
    }

    for (i=0;i<wkt_size;i++) {
        char *wkt = builder.get_wkt(i);

        if (wkt && strlen(wkt)) {
            expire->from_wkt(wkt, -id);
            /* FIXME: there should be a better way to detect polygons */
            if (!strncmp(wkt, "POLYGON", strlen("POLYGON")) || !strncmp(wkt, "MULTIPOLYGON", strlen("MULTIPOLYGON"))) {
                double area = builder.get_area(i);
                if ((area > 0.0) && m_enable_way_area) {
                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "%g", area);
                    addItem(rel_tags, "way_area", tmp, 0);
                }
                write_wkts(-id, rel_tags, wkt, t_poly, sql);
            } else {
                write_wkts(-id, rel_tags, wkt, t_line, sql);
                if (roads)
                    write_wkts(-id, rel_tags, wkt, t_roads, sql);
            }
        }
        free(wkt);
    }

    builder.clear_wkts();

    /* Tagtransform will have marked those member ways of the relation that
     * have fully been dealt with as part of the multi-polygon entry.
     * Set them in the database as done and delete their entry to not
     * have duplicates */
    if (make_polygon) {
        for (i=0; xcount[i]; i++) {
            if (members_superseeded[i]) {
                ways_done_tracker->mark(xid[i]);
                pgsql_delete_way_from_output(xid[i]);
            }
        }
    }

    free(members_superseeded);

    /* If we are making a boundary then also try adding any relations which form complete rings
       The linear variants will have already been processed above */
    if (make_boundary) {
        wkt_size = builder.build(id, xnodes, xcount, 1, m_options->enable_multi, split_at);
        for (i=0;i<wkt_size;i++)
        {
            char *wkt = builder.get_wkt(i);

            if (strlen(wkt)) {
                expire->from_wkt(wkt, -id);
                /* FIXME: there should be a better way to detect polygons */
                if (!strncmp(wkt, "POLYGON", strlen("POLYGON")) || !strncmp(wkt, "MULTIPOLYGON", strlen("MULTIPOLYGON"))) {
                    double area = builder.get_area(i);
                    if ((area > 0.0) && m_enable_way_area) {
                        char tmp[32];
                        snprintf(tmp, sizeof(tmp), "%g", area);
                        addItem(rel_tags, "way_area", tmp, 0);
                    }
                    write_wkts(-id, rel_tags, wkt, t_poly, sql);
                }
            }
            free(wkt);
        }
        builder.clear_wkts();
    }

    return 0;
}

int output_pgsql_t::connect(int startTransaction) {
    int i;
    for (i=0; i<NUM_TABLES; i++) {
        PGconn *sql_conn;
        sql_conn = PQconnectdb(m_options->conninfo);
        
        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
            return 1;
        }
        m_tables[i].sql_conn = sql_conn;
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "PREPARE get_wkt (" POSTGRES_OSMID_TYPE ") AS SELECT ST_AsText(way) FROM %s WHERE osm_id = $1;\n", m_tables[i].name);
        if (startTransaction)
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "BEGIN");
    }

    if (ways_pending_tracker) { ways_pending_tracker->force_release(); }
    if (ways_done_tracker) { ways_done_tracker->force_release(); }
    if (rels_pending_tracker) { rels_pending_tracker->force_release(); }
    ways_pending_tracker.reset(new pgsql_id_tracker(m_options->conninfo, m_options->prefix, "ways_pending", false));
    ways_done_tracker.reset(new pgsql_id_tracker(m_options->conninfo, m_options->prefix, "ways_done", false));
    rels_pending_tracker.reset(new pgsql_id_tracker(m_options->conninfo, m_options->prefix, "rels_pending", false));

    return 0;
}

int output_pgsql_t::start()
{
    char *sql, tmp[256];
    PGresult   *res;
    int i,j;
    unsigned int sql_len;
    int their_srid;
    int i_hstore_column;
    enum OsmType type;
    int numTags;

    reproj = m_options->projection;
    builder.set_exclude_broken_polygon(m_options->excludepoly);

    /* Tables to output */
    m_tables.reserve(NUM_TABLES);
    m_tables.push_back(table("%s_point",   "POINT"));
    m_tables.push_back(table("%s_line",    "LINESTRING"));
    m_tables.push_back(table("%s_polygon", "GEOMETRY"  )); /* Actually POLGYON & MULTIPOLYGON but no way to limit to just these two */
    m_tables.push_back(table("%s_roads",   "LINESTRING"));

    m_export_list = new export_list;

    m_enable_way_area = read_style_file( m_options->style, m_export_list );

    sql_len = 2048;
    sql = (char *)malloc(sql_len);
    assert(sql);

    for (i=0; i<NUM_TABLES; i++) {
        PGconn *sql_conn;

        /* Substitute prefix into name of table */
        {
            char *temp = (char *)malloc( strlen(m_options->prefix) + strlen(m_tables[i].name) + 1 );
            sprintf( temp, m_tables[i].name, m_options->prefix );
            m_tables[i].name = temp;
        }
        fprintf(stderr, "Setting up table: %s\n", m_tables[i].name);
        sql_conn = PQconnectdb(m_options->conninfo);

        /* Check to see that the backend connection was successfully made */
        if (PQstatus(sql_conn) != CONNECTION_OK) {
            fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(sql_conn));
            exit_nicely();
        }
        m_tables[i].sql_conn = sql_conn;
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "SET synchronous_commit TO off;");

        if (!m_options->append) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s", m_tables[i].name);
        }
        else
        {
            sprintf(sql, "SELECT srid FROM geometry_columns WHERE f_table_name='%s';", m_tables[i].name);
            res = PQexec(sql_conn, sql);
            if (!((PQntuples(res) == 1) && (PQnfields(res) == 1)))
            {
                fprintf(stderr, "Problem reading geometry information for table %s - does it exist?\n", m_tables[i].name);
                exit_nicely();
            }
            their_srid = atoi(PQgetvalue(res, 0, 0));
            PQclear(res);
            if (their_srid != SRID)
            {
                fprintf(stderr, "SRID mismatch: cannot append to table %s (SRID %d) using selected SRID %d\n", m_tables[i].name, their_srid, SRID);
                exit_nicely();
            }
        }

        /* These _tmp tables can be left behind if we run out of disk space */
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE IF EXISTS %s_tmp", m_tables[i].name);

        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "BEGIN");

        type = (i == t_point)?OSMTYPE_NODE:OSMTYPE_WAY;
        const std::vector<taginfo> &infos = m_export_list->get(type);
        numTags = infos.size();
        if (!m_options->append) {
            sprintf(sql, "CREATE TABLE %s ( osm_id " POSTGRES_OSMID_TYPE, m_tables[i].name );
            for (j=0; j < numTags; j++) {
                const taginfo &info = infos[j];
                if( info.flags & FLAG_DELETE )
                    continue;
                if( (info.flags & FLAG_PHSTORE ) == FLAG_PHSTORE)
                    continue;
                sprintf(tmp, ",\"%s\" %s", info.name.c_str(), info.type.c_str());
                if (strlen(sql) + strlen(tmp) + 1 > sql_len) {
                    sql_len *= 2;
                    sql = (char *)realloc(sql, sql_len);
                    assert(sql);
                }
                strcat(sql, tmp);
            }
            for(i_hstore_column = 0; i_hstore_column < m_options->n_hstore_columns; i_hstore_column++)
            {
                strcat(sql, ",\"");
                strcat(sql, m_options->hstore_columns[i_hstore_column]);
                strcat(sql, "\" hstore ");
            }
            if (m_options->enable_hstore) {
                strcat(sql, ",tags hstore");
            } 
            strcat(sql, ")");
            if (m_options->tblsmain_data) {
                sprintf(sql + strlen(sql), " TABLESPACE %s", m_options->tblsmain_data);
            }
            strcat(sql, "\n");

            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", sql);
            pgsql_exec(sql_conn, PGRES_TUPLES_OK, "SELECT AddGeometryColumn('%s', 'way', %d, '%s', 2 );\n",
                        m_tables[i].name, SRID, m_tables[i].type );
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ALTER TABLE %s ALTER COLUMN way SET NOT NULL;\n", m_tables[i].name);
            /* slim mode needs this to be able to apply diffs */
            if (m_options->slim && !m_options->droptemp) {
                sprintf(sql, "CREATE INDEX %s_pkey ON %s USING BTREE (osm_id)",  m_tables[i].name, m_tables[i].name);
                if (m_options->tblsmain_index) {
                    sprintf(sql + strlen(sql), " TABLESPACE %s\n", m_options->tblsmain_index);
                }
	            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "%s", sql);
            }
        } else {
            /* Add any new columns referenced in the default.style */
            PGresult *res;
            sprintf(sql, "SELECT * FROM %s LIMIT 0;\n", m_tables[i].name);
            res = PQexec(sql_conn, sql);
            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                fprintf(stderr, "Error, failed to query table %s\n%s\n", m_tables[i].name, sql);
                exit_nicely();
            }
            for (j=0; j < numTags; j++) {
                const taginfo &info = infos[j];
                if( info.flags & FLAG_DELETE )
                    continue;
                if( (info.flags & FLAG_PHSTORE) == FLAG_PHSTORE)
                    continue;
                sprintf(tmp, "\"%s\"", info.name.c_str());
                if (PQfnumber(res, tmp) < 0) {
#if 0
                    fprintf(stderr, "Append failed. Column \"%s\" is missing from \"%s\"\n", info.name.c_str(), m_tables[i].name);
                    exit_nicely();
#else
                    fprintf(stderr, "Adding new column \"%s\" to \"%s\"\n", info.name.c_str(), m_tables[i].name);
                    pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ALTER TABLE %s ADD COLUMN \"%s\" %s;\n", m_tables[i].name, info.name.c_str(), info.type.c_str());
#endif
                }
                /* Note: we do not verify the type or delete unused columns */
            }

            PQclear(res);

            /* change the type of the geometry column if needed - this can only change to a more permisive type */
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "PREPARE get_wkt (" POSTGRES_OSMID_TYPE ") AS SELECT ST_AsText(way) FROM %s WHERE osm_id = $1;\n", m_tables[i].name);
        
        /* Generate column list for COPY */
        strcpy(sql, "osm_id");
        for (j=0; j < numTags; j++) {
            const taginfo &info = infos[j];
            if( info.flags & FLAG_DELETE )
                continue;
            if( (info.flags & FLAG_PHSTORE ) == FLAG_PHSTORE)
                continue;
            sprintf(tmp, ",\"%s\"", info.name.c_str());

            if (strlen(sql) + strlen(tmp) + 1 > sql_len) {
                sql_len *= 2;
                sql = (char *)realloc(sql, sql_len);
                assert(sql);
            }
            strcat(sql, tmp);
        }

        for(i_hstore_column = 0; i_hstore_column < m_options->n_hstore_columns; i_hstore_column++)
        {
            strcat(sql, ",\"");
            strcat(sql, m_options->hstore_columns[i_hstore_column]);
            strcat(sql, "\" ");
        }
    
	if (m_options->enable_hstore) strcat(sql,",tags");

	m_tables[i].columns = strdup(sql);
        pgsql_exec(sql_conn, PGRES_COPY_IN, "COPY %s (%s,way) FROM STDIN", m_tables[i].name, m_tables[i].columns);

        m_tables[i].copyMode = 1;
    }
    free(sql);

    try {
    	m_tagtransform = new tagtransform(m_options);
    }
    catch(std::runtime_error& e) {
    	fprintf(stderr, "%s\n", e.what());
        fprintf(stderr, "Error: Failed to initialise tag processing.\n");
        exit_nicely();
    }
    expire.reset(new expire_tiles(m_options));

    ways_pending_tracker.reset(new pgsql_id_tracker(m_options->conninfo, m_options->prefix, "ways_pending", true));
    ways_done_tracker.reset(new pgsql_id_tracker(m_options->conninfo, m_options->prefix, "ways_done", true));
    rels_pending_tracker.reset(new pgsql_id_tracker(m_options->conninfo, m_options->prefix, "rels_pending", true));

    return 0;
}

void output_pgsql_t::pgsql_pause_copy(output_pgsql_t::table *table)
{
    PGresult   *res;
    int stop;
    
    if( !table->copyMode )
        return;
        
    /* Terminate any pending COPY */
    stop = PQputCopyEnd(table->sql_conn, NULL);
    if (stop != 1) {
       fprintf(stderr, "COPY_END for %s failed: %s\n", table->name, PQerrorMessage(table->sql_conn));
       exit_nicely();
    }

    res = PQgetResult(table->sql_conn);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
       fprintf(stderr, "COPY_END for %s failed: %s\n", table->name, PQerrorMessage(table->sql_conn));
       PQclear(res);
       exit_nicely();
    }
    PQclear(res);
    table->copyMode = 0;
}

void output_pgsql_t::close(int stopTransaction) {
    int i;
    for (i=0; i<NUM_TABLES; i++) {
        pgsql_pause_copy(&m_tables[i]);
        /* Commit transaction */
        if (stopTransaction)
            pgsql_exec(m_tables[i].sql_conn, PGRES_COMMAND_OK, "COMMIT");
        PQfinish(m_tables[i].sql_conn);
        m_tables[i].sql_conn = NULL;
    }
}

void output_pgsql_t::pgsql_out_commit(void) {
    int i;
    for (i=0; i<NUM_TABLES; i++) {
        pgsql_pause_copy(&m_tables[i]);
        /* Commit transaction */
        fprintf(stderr, "Committing transaction for %s\n", m_tables[i].name);
        pgsql_exec(m_tables[i].sql_conn, PGRES_COMMAND_OK, "COMMIT");
    }
}

void *output_pgsql_t::pgsql_out_stop_one(void *arg)
{
    int i_column;
    output_pgsql_t::table *table = (output_pgsql_t::table *)arg;
    PGconn *sql_conn = table->sql_conn;

    if( table->buflen != 0 )
    {
       fprintf( stderr, "Internal error: Buffer for %s has %d bytes after end copy", table->name, table->buflen );
       exit_nicely();
    }

    pgsql_pause_copy(table);
    if (!m_options->append)
    {
        time_t start, end;
        time(&start);
        fprintf(stderr, "Sorting data and creating indexes for %s\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ANALYZE %s;\n", table->name);
        fprintf(stderr, "Analyzing %s finished\n", table->name);
        if (m_options->tblsmain_data) {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE TABLE %s_tmp "
                        "TABLESPACE %s AS SELECT * FROM %s ORDER BY way;\n",
                        table->name, m_options->tblsmain_data, table->name);
        } else {
            pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE TABLE %s_tmp AS SELECT * FROM %s ORDER BY way;\n", table->name, table->name);
        }
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "DROP TABLE %s;\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ALTER TABLE %s_tmp RENAME TO %s;\n", table->name, table->name);
        fprintf(stderr, "Copying %s to cluster by geometry finished\n", table->name);
        fprintf(stderr, "Creating geometry index on  %s\n", table->name);
        if (m_options->tblsmain_index) {
            /* Use fillfactor 100 for un-updatable imports */
            if (m_options->slim && !m_options->droptemp) {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way) TABLESPACE %s;\n", table->name, table->name, m_options->tblsmain_index);
            } else {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way) WITH (FILLFACTOR=100) TABLESPACE %s;\n", table->name, table->name, m_options->tblsmain_index);
            }
        } else {
            if (m_options->slim && !m_options->droptemp) {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way);\n", table->name, table->name);
            } else {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_index ON %s USING GIST (way) WITH (FILLFACTOR=100);\n", table->name, table->name);
            }
        }

        /* slim mode needs this to be able to apply diffs */
        if (m_options->slim && !m_options->droptemp)
        {
            fprintf(stderr, "Creating osm_id index on  %s\n", table->name);
            if (m_options->tblsmain_index) {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_pkey ON %s USING BTREE (osm_id) TABLESPACE %s;\n", table->name, table->name, m_options->tblsmain_index);
            } else {
                pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_pkey ON %s USING BTREE (osm_id);\n", table->name, table->name);
            }
        }
        /* Create hstore index if selected */
        if (m_options->enable_hstore_index) {
            fprintf(stderr, "Creating hstore indexes on  %s\n", table->name);
            if (m_options->tblsmain_index) {
                if (HSTORE_NONE != (m_options->enable_hstore)) {
                    if (m_options->slim && !m_options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags) TABLESPACE %s;\n", table->name, table->name, m_options->tblsmain_index);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags) TABLESPACE %s;\n", table->name, table->name, m_options->tblsmain_index);
                    }
                }
                for(i_column = 0; i_column < m_options->n_hstore_columns; i_column++) {
                    if (m_options->slim && !m_options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\") TABLESPACE %s;\n",
                               table->name, i_column,table->name, m_options->hstore_columns[i_column], m_options->tblsmain_index);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\") TABLESPACE %s;\n",
                               table->name, i_column,table->name, m_options->hstore_columns[i_column], m_options->tblsmain_index);
                    }
                }
            } else {
                if (HSTORE_NONE != (m_options->enable_hstore)) {
                    if (m_options->slim && !m_options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags);\n", table->name, table->name);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_tags_index ON %s USING GIN (tags) ;\n", table->name, table->name);
                    }
                }
                for(i_column = 0; i_column < m_options->n_hstore_columns; i_column++) {
                    if (m_options->slim && !m_options->droptemp) {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\");\n", table->name, i_column,table->name, m_options->hstore_columns[i_column]);
                    } else {
                        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "CREATE INDEX %s_hstore_%i_index ON %s USING GIN (\"%s\");\n", table->name, i_column,table->name, m_options->hstore_columns[i_column]);
                    }
                }
            }
        }
        fprintf(stderr, "Creating indexes on  %s finished\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "GRANT SELECT ON %s TO PUBLIC;\n", table->name);
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "ANALYZE %s;\n", table->name);
        time(&end);
        fprintf(stderr, "All indexes on  %s created  in %ds\n", table->name, (int)(end - start));
    }
    PQfinish(sql_conn);
    table->sql_conn = NULL;

    fprintf(stderr, "Completed %s\n", table->name);
    free(table->name);
    free(table->columns);
    return NULL;
}

namespace {
/* Using pthreads requires us to shoe-horn everything into various void*
 * pointers. Improvement for the future: just use boost::thread. */
struct pthread_thunk {
    output_pgsql_t *obj;
    void *ptr;
};

extern "C" void *pthread_output_pgsql_stop_one(void *arg) {
    pthread_thunk *thunk = static_cast<pthread_thunk *>(arg);
    return thunk->obj->pgsql_out_stop_one(thunk->ptr);
};
} // anonymous namespace

output_pgsql_t::way_cb_func::way_cb_func(output_pgsql_t *ptr)
    : m_ptr(ptr), m_sql(),
      m_next_internal_id(m_ptr->ways_pending_tracker->pop_mark()) {
}

output_pgsql_t::way_cb_func::~way_cb_func() {}

int output_pgsql_t::way_cb_func::operator()(osmid_t id, struct keyval *tags, struct osmNode *nodes, int count, int exists) {
    if (m_next_internal_id < id) {
        run_internal_until(id, exists);
    }

    if (m_next_internal_id == id) {
        m_next_internal_id = m_ptr->ways_pending_tracker->pop_mark();
    }

    if (m_ptr->ways_done_tracker->is_marked(id)) {
        return 0;
    } else {
        return m_ptr->pgsql_out_way(id, tags, nodes, count, exists, m_sql);
    }
}

void output_pgsql_t::way_cb_func::finish(int exists) {
    run_internal_until(std::numeric_limits<osmid_t>::max(), exists);
}

void output_pgsql_t::way_cb_func::run_internal_until(osmid_t id, int exists) {
    struct keyval tags_int;
    struct osmNode *nodes_int;
    int count_int;
    
    while (m_next_internal_id < id) {
        initList(&tags_int);
        if (!m_ptr->m_mid->ways_get(m_next_internal_id, &tags_int, &nodes_int, &count_int)) {
            if (!m_ptr->ways_done_tracker->is_marked(m_next_internal_id)) {
                m_ptr->pgsql_out_way(m_next_internal_id, &tags_int, nodes_int, count_int, exists, m_sql);
            }
            
            free(nodes_int);
        }
        resetList(&tags_int);
        
        m_next_internal_id = m_ptr->ways_pending_tracker->pop_mark();
    }
}

output_pgsql_t::rel_cb_func::rel_cb_func(output_pgsql_t *ptr)
    : m_ptr(ptr), m_sql(),
      m_next_internal_id(m_ptr->rels_pending_tracker->pop_mark()) {
}

output_pgsql_t::rel_cb_func::~rel_cb_func() {}

int output_pgsql_t::rel_cb_func::operator()(osmid_t id, struct member *mems, int member_count, struct keyval *rel_tags, int exists) {
    if (m_next_internal_id < id) {
        run_internal_until(id, exists);
    }

    if (m_next_internal_id == id) {
        m_next_internal_id = m_ptr->rels_pending_tracker->pop_mark();
    }

    return m_ptr->pgsql_process_relation(id, mems, member_count, rel_tags, exists, m_sql);
}

void output_pgsql_t::rel_cb_func::finish(int exists) {
    run_internal_until(std::numeric_limits<osmid_t>::max(), exists);
}

void output_pgsql_t::rel_cb_func::run_internal_until(osmid_t id, int exists) {
    struct keyval tags_int;
    struct member *members_int;
    int count_int;
    
    while (m_next_internal_id < id) {
        initList(&tags_int);
        if (!m_ptr->m_mid->relations_get(m_next_internal_id, &members_int, &count_int, &tags_int)) {
            m_ptr->pgsql_process_relation(m_next_internal_id, members_int, count_int, &tags_int, exists, m_sql);
            
            free(members_int);
        }
        resetList(&tags_int);
        
        m_next_internal_id = m_ptr->rels_pending_tracker->pop_mark();
    }
}

void output_pgsql_t::commit()
{
    pgsql_out_commit();
    ways_pending_tracker->commit();
    ways_done_tracker->commit();
    rels_pending_tracker->commit();
}

middle_t::way_cb_func *output_pgsql_t::way_callback()
{
    /* To prevent deadlocks in parallel processing, the mid tables need
     * to stay out of a transaction. In this stage output tables are only
     * written to and not read, so they can be processed as several parallel
     * independent transactions
     */
    for (int i=0; i<NUM_TABLES; i++) {
        PGconn *sql_conn = m_tables[i].sql_conn;
        pgsql_exec(sql_conn, PGRES_COMMAND_OK, "BEGIN");
    }

    /* Processing any remaing to be processed ways */
    way_cb_func *func = new way_cb_func(this);

    return func;
}

middle_t::rel_cb_func *output_pgsql_t::relation_callback()
{
    /* Processing any remaing to be processed relations */
    /* During this stage output tables also need to stay out of
     * extended transactions, as the delete_way_from_output, called
     * from process_relation, can deadlock if using multi-processing.
     */
    rel_cb_func *rel_callback = new rel_cb_func(this);
    return rel_callback;
}

void output_pgsql_t::stop()
{
    int i;
#ifdef HAVE_PTHREAD
    pthread_t threads[NUM_TABLES];
#endif
  
#ifdef HAVE_PTHREAD
    if (m_options->parallel_indexing) {
      pthread_thunk thunks[NUM_TABLES];
      for (i=0; i<NUM_TABLES; i++) {
          thunks[i].obj = this;
          thunks[i].ptr = &m_tables[i];
      }

      for (i=0; i<NUM_TABLES; i++) {
          int ret = pthread_create(&threads[i], NULL, pthread_output_pgsql_stop_one, &thunks[i]);
          if (ret) {
              fprintf(stderr, "pthread_create() returned an error (%d)", ret);
              exit_nicely();
          }
      }
  
      for (i=0; i<NUM_TABLES; i++) {
          int ret = pthread_join(threads[i], NULL);
          if (ret) {
              fprintf(stderr, "pthread_join() returned an error (%d)", ret);
              exit_nicely();
          }
      }
    } else {
#endif

    /* No longer need to access middle layer -- release memory */
    for (i=0; i<NUM_TABLES; i++)
        pgsql_out_stop_one(&m_tables[i]);

#ifdef HAVE_PTHREAD
    }
#endif


    cleanup();
    delete m_export_list;

    expire.reset();
}

int output_pgsql_t::node_add(osmid_t id, double lat, double lon, struct keyval *tags)
{
  pgsql_out_node(id, tags, lat, lon, m_sql);

  return 0;
}

int output_pgsql_t::way_add(osmid_t id, osmid_t *nds, int nd_count, struct keyval *tags)
{
  int polygon = 0;
  int roads = 0;


  /* Check whether the way is: (1) Exportable, (2) Maybe a polygon */
  int filter = m_tagtransform->filter_way_tags(tags, &polygon, &roads, m_export_list);

  /* If this isn't a polygon then it can not be part of a multipolygon
     Hence only polygons are "pending" */
  if (!filter && polygon) { ways_pending_tracker->mark(id); }

  if( !polygon && !filter )
  {
    /* Get actual node data and generate output */
    struct osmNode *nodes = (struct osmNode *)malloc( sizeof(struct osmNode) * nd_count );
    int count = m_mid->nodes_get_list( nodes, nds, nd_count );
    pgsql_out_way(id, tags, nodes, count, 0, m_sql);
    free(nodes);
  }
  return 0;
}

/* This is the workhorse of pgsql_add_relation, split out because it is used as the callback for iterate relations */
int output_pgsql_t::pgsql_process_relation(osmid_t id, struct member *members, int member_count, struct keyval *tags, int exists, buffer &sql)
{
    int i, j, count, count2;
    osmid_t *xid2 = (osmid_t *)malloc( (member_count+1) * sizeof(osmid_t) );
  osmid_t *xid;
  const char **xrole = (const char **)malloc( (member_count+1) * sizeof(const char *) );
  int *xcount = (int *)malloc( (member_count+1) * sizeof(int) );
  struct keyval *xtags  = (struct keyval *)malloc( (member_count+1) * sizeof(struct keyval) );
  struct osmNode **xnodes = (struct osmNode **)malloc( (member_count+1) * sizeof(struct osmNode*) );

  /* If the flag says this object may exist already, delete it first */
  if(exists)
      pgsql_delete_relation_from_output(id);

  if (m_tagtransform->filter_rel_tags(tags, m_export_list)) {
      free(xid2);
      free(xrole);
      free(xcount);
      free(xtags);
      free(xnodes);
      return 1;
  }

  count = 0;
  for( i=0; i<member_count; i++ )
  {
  
    /* Need to handle more than just ways... */
    if( members[i].type != OSMTYPE_WAY )
        continue;
    xid2[count] = members[i].id;
    count++;
  }

  count2 = m_mid->ways_get_list(xid2, count, &xid, xtags, xnodes, xcount);

  for (i = 0; i < count2; i++) {
      for (j = i; j < member_count; j++) {
          if (members[j].id == xid[i]) break;
      }
      xrole[i] = members[j].role;
  }
  xnodes[count2] = NULL;
  xcount[count2] = 0;
  xid[count2] = 0;
  xrole[count2] = NULL;

  /* At some point we might want to consider storing the retrieved data in the members, rather than as separate arrays */
  pgsql_out_relation(id, tags, count2, xnodes, xtags, xcount, xid, xrole, sql);

  for( i=0; i<count2; i++ )
  {
    resetList( &(xtags[i]) );
    free( xnodes[i] );
  }

  free(xid2);
  free(xid);
  free(xrole);
  free(xcount);
  free(xtags);
  free(xnodes);
  return 0;
}

int output_pgsql_t::relation_add(osmid_t id, struct member *members, int member_count, struct keyval *tags)
{
  const char *type = getItem(tags, "type");

  /* Must have a type field or we ignore it */
  if (!type)
      return 0;

  /* Only a limited subset of type= is supported, ignore other */
  if ( (strcmp(type, "route") != 0) && (strcmp(type, "multipolygon") != 0) && (strcmp(type, "boundary") != 0))
    return 0;


  return pgsql_process_relation(id, members, member_count, tags, 0, m_sql);
}
#define UNUSED  __attribute__ ((unused))

/* Delete is easy, just remove all traces of this object. We don't need to
 * worry about finding objects that depend on it, since the same diff must
 * contain the change for that also. */
int output_pgsql_t::node_delete(osmid_t osm_id)
{
    if( !m_options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_pause_copy(&m_tables[t_point]);
    if ( expire->from_db(m_tables[t_point].sql_conn, osm_id) != 0)
        pgsql_exec(m_tables[t_point].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, m_tables[t_point].name, osm_id );
    
    return 0;
}

/* Seperated out because we use it elsewhere */
int output_pgsql_t::pgsql_delete_way_from_output(osmid_t osm_id)
{
    /* Optimisation: we only need this is slim mode */
    if( !m_options->slim )
        return 0;
    /* in droptemp mode we don't have indices and this takes ages. */
    if (m_options->droptemp)
        return 0;
    pgsql_pause_copy(&m_tables[t_roads]);
    pgsql_pause_copy(&m_tables[t_line]);
    pgsql_pause_copy(&m_tables[t_poly]);
    pgsql_exec(m_tables[t_roads].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, m_tables[t_roads].name, osm_id );
    if ( expire->from_db(m_tables[t_line].sql_conn, osm_id) != 0)
        pgsql_exec(m_tables[t_line].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, m_tables[t_line].name, osm_id );
    if ( expire->from_db(m_tables[t_poly].sql_conn, osm_id) != 0)
        pgsql_exec(m_tables[t_poly].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, m_tables[t_poly].name, osm_id );
    return 0;
}

int output_pgsql_t::way_delete(osmid_t osm_id)
{
    if( !m_options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_delete_way_from_output(osm_id);
    return 0;
}

/* Relations are identified by using negative IDs */
int output_pgsql_t::pgsql_delete_relation_from_output(osmid_t osm_id)
{
    pgsql_pause_copy(&m_tables[t_roads]);
    pgsql_pause_copy(&m_tables[t_line]);
    pgsql_pause_copy(&m_tables[t_poly]);
    pgsql_exec(m_tables[t_roads].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, m_tables[t_roads].name, -osm_id );
    if ( expire->from_db(m_tables[t_line].sql_conn, -osm_id) != 0)
        pgsql_exec(m_tables[t_line].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, m_tables[t_line].name, -osm_id );
    if ( expire->from_db(m_tables[t_poly].sql_conn, -osm_id) != 0)
        pgsql_exec(m_tables[t_poly].sql_conn, PGRES_COMMAND_OK, "DELETE FROM %s WHERE osm_id = %" PRIdOSMID, m_tables[t_poly].name, -osm_id );
    return 0;
}

int output_pgsql_t::relation_delete(osmid_t osm_id)
{
    if( !m_options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    pgsql_delete_relation_from_output(osm_id);
    return 0;
}

/* Modify is slightly trickier. The basic idea is we simply delete the
 * object and create it with the new parameters. Then we need to mark the
 * objects that depend on this one */
int output_pgsql_t::node_modify(osmid_t osm_id, double lat, double lon, struct keyval *tags)
{
    if( !m_options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    node_delete(osm_id);
    node_add(osm_id, lat, lon, tags);
    return 0;
}

int output_pgsql_t::way_modify(osmid_t osm_id, osmid_t *nodes, int node_count, struct keyval *tags)
{
    if( !m_options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    way_delete(osm_id);
    way_add(osm_id, nodes, node_count, tags);

    return 0;
}

int output_pgsql_t::relation_modify(osmid_t osm_id, struct member *members, int member_count, struct keyval *tags)
{
    if( !m_options->slim )
    {
        fprintf( stderr, "Cannot apply diffs unless in slim mode\n" );
        exit_nicely();
    }
    relation_delete(osm_id);
    relation_add(osm_id, members, member_count, tags);
    return 0;
}

output_pgsql_t::output_pgsql_t(middle_t* mid_, const output_options* options_)
    : output_t(mid_, options_) {
}

output_pgsql_t::~output_pgsql_t() {
    if(m_tagtransform != NULL)
    	delete m_tagtransform;
}
