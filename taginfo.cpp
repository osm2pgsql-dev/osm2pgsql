#include "taginfo_impl.hpp"
#include "table.hpp"
#include "util.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <boost/format.hpp>
#include <errno.h>

#ifdef _WIN32
#ifndef strtok_r
 #define strtok_r strtok_s
#endif
#endif

/* NOTE: section below for flags genuinely is static and
 * constant, so there's no need to hoist this into a per
 * class variable. It doesn't get modified, so it's safe
 * to share across threads and its lifetime is the whole
 * program.
 */
struct flagsname {
    flagsname(const char *name_, int flag_)
        : name(name_), flag(flag_) {
    }
    const char *name;
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

taginfo::taginfo()
    : name(), type(), flags(0) {
}

taginfo::taginfo(const taginfo &other)
    : name(other.name), type(other.type),
      flags(other.flags) {
}

export_list::export_list()
    : num_tables(0), exportList() {
}

void export_list::add(enum OsmType id, const taginfo &info) {
    std::vector<taginfo> &infos = get(id);
    infos.push_back(info);
}

std::vector<taginfo> &export_list::get(enum OsmType id) {
    if (id >= num_tables) {
        exportList.resize(id+1);
        num_tables = id + 1;
    }
    return exportList[id];
}

const std::vector<taginfo> &export_list::get(enum OsmType id) const {
    // this fakes as if we have infinite taginfo vectors, but
    // means we don't actually have anything allocated unless
    // the info object has been assigned.
    static const std::vector<taginfo> empty;

    if (id < num_tables) {
        return exportList[id];
    } else {
        return empty;
    }
}

columns_t export_list::normal_columns(enum OsmType id) const {
    columns_t columns;
    const std::vector<taginfo> &infos = get(id);
    for(std::vector<taginfo>::const_iterator info = infos.begin(); info != infos.end(); ++info)
    {
        if( info->flags & FLAG_DELETE )
            continue;
        if( (info->flags & FLAG_PHSTORE ) == FLAG_PHSTORE)
            continue;
        columns.push_back(std::pair<std::string, std::string>(info->name, info->type));
    }
    return columns;
}

int parse_tag_flags(const char *flags_, int lineno) {
    int temp_flags = 0;
    char *str = NULL, *saveptr = NULL;
    int i = 0;

    // yuck! but strtok requires a non-const char * pointer, and i'm fairly sure it
    // doesn't actually modify the string.
    char *flags = const_cast<char *>(flags_);

    //split the flags column on commas and keep track of which flags you've seen in a bit mask
    for(str = strtok_r(flags, ",\r\n", &saveptr); str != NULL; str = strtok_r(NULL, ",\r\n", &saveptr))
    {
      for( i=0; i<NUM_FLAGS; i++ )
      {
        if( strcmp( tagflags[i].name, str ) == 0 )
        {
          temp_flags |= tagflags[i].flag;
          break;
        }
      }
      if( i == NUM_FLAGS )
        fprintf( stderr, "Unknown flag '%s' line %d, ignored\n", str, lineno );
    }

    return temp_flags;
}

int read_style_file( const std::string &filename, export_list *exlist )
{
  FILE *in;
  int lineno = 0;
  int num_read = 0;
  char osmtype[24];
  char tag[64];
  char datatype[24];
  char flags[128];
  char *str;
  int fields;
  struct taginfo temp;
  char buffer[1024];
  int enable_way_area = 1;

  in = fopen( filename.c_str(), "rt" );
  if( !in )
  {
      throw std::runtime_error((boost::format("Couldn't open style file '%1%': %2%")
                                % filename % strerror(errno)).str());
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
      util::exit_nicely();
    }

    //place to keep info about this tag
    temp.name.assign(tag);
    temp.type.assign(datatype);
    temp.flags = parse_tag_flags(flags, lineno);

    if ((temp.flags != FLAG_DELETE) &&
        ((temp.name.find('?') != std::string::npos) ||
         (temp.name.find('*') != std::string::npos))) {
        fprintf( stderr, "wildcard '%s' in non-delete style entry\n",temp.name.c_str());
        util::exit_nicely();
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
        throw std::runtime_error((boost::format("Weird style line %1%:%2%")
                                  % filename % lineno).str());
    }
    num_read++;
  }


  if (ferror(in)) {
      throw std::runtime_error((boost::format("%1%: %2%")
                                % filename % strerror(errno)).str());
  }
  if (num_read == 0) {
      throw std::runtime_error("Unable to parse any valid columns from "
                               "the style file. Aborting.");
  }
  fclose(in);
  return enable_way_area;
}
