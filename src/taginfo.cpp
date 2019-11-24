#include "format.hpp"
#include "taginfo-impl.hpp"
#include "util.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <map>
#include <stdexcept>

#include <osmium/util/string.hpp>

static const std::map<std::string, unsigned> tagflags = {
    {"polygon", FLAG_POLYGON}, {"linear", FLAG_LINEAR},
    {"nocache", FLAG_NOCACHE}, {"delete", FLAG_DELETE},
    {"phstore", FLAG_PHSTORE}, {"nocolumn", FLAG_NOCOLUMN}};

static const std::map<std::string, unsigned> tagtypes = {
    {"smallint", FLAG_INT_TYPE}, {"integer", FLAG_INT_TYPE},
    {"bigint", FLAG_INT_TYPE},   {"int2", FLAG_INT_TYPE},
    {"int4", FLAG_INT_TYPE},     {"int8", FLAG_INT_TYPE},
    {"real", FLAG_REAL_TYPE},    {"double precision", FLAG_REAL_TYPE}};

taginfo::taginfo() : name(), type(), flags(0) {}

taginfo::taginfo(const taginfo &other)
: name(other.name), type(other.type), flags(other.flags)
{}

void export_list::add(osmium::item_type id, const taginfo &info)
{
    std::vector<taginfo> &infos = get(id);
    infos.push_back(info);
}

std::vector<taginfo> &export_list::get(osmium::item_type id)
{
    auto idx = item_type_to_nwr_index(id);
    if (idx >= exportList.size()) {
        exportList.resize(idx + 1);
    }
    return exportList[idx];
}

const std::vector<taginfo> &export_list::get(osmium::item_type id) const
{
    // this fakes as if we have infinite taginfo vectors, but
    // means we don't actually have anything allocated unless
    // the info object has been assigned.
    static const std::vector<taginfo> empty;

    auto idx = item_type_to_nwr_index(id);
    if (idx < exportList.size()) {
        return exportList[idx];
    } else {
        return empty;
    }
}

bool export_list::has_column(osmium::item_type id, char const *name) const
{
    auto idx = item_type_to_nwr_index(id);
    if (idx >= exportList.size()) {
        return false;
    }

    for (auto const &info : exportList[idx]) {
        if (info.name == name) {
            return true;
        }
    }

    return false;
}

columns_t export_list::normal_columns(osmium::item_type id) const
{
    columns_t columns;

    for (auto const &info : get(id)) {
        if (!(info.flags & (FLAG_DELETE | FLAG_NOCOLUMN))) {
            columns.emplace_back(info.name, info.type, info.column_type());
        }
    }

    return columns;
}

unsigned parse_tag_flags(std::string const &flags, int lineno)
{
    unsigned temp_flags = 0;

    for (auto const &flag_name : osmium::split_string(flags, ",\r\n")) {
        auto const it = tagflags.find(flag_name);
        if (it != tagflags.end()) {
            temp_flags |= it->second;
        } else {
            fprintf(stderr, "Unknown flag '%s' line %d, ignored\n",
                    flag_name.c_str(), lineno);
        }
    }

    return temp_flags;
}

int read_style_file(const std::string &filename, export_list *exlist)
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

    in = fopen(filename.c_str(), "rt");
    if (!in) {
        throw std::runtime_error{"Couldn't open style file '{}': {}"_format(
            filename, std::strerror(errno))};
    }

    //for each line of the style file
    while (fgets(buffer, sizeof(buffer), in) != nullptr) {
        lineno++;

        //find where a comment starts and terminate the string there
        str = strchr(buffer, '#');
        if (str) {
            *str = '\0';
        }

        //grab the expected fields for this row
        fields = sscanf(buffer, "%23s %63s %23s %127s", osmtype, tag, datatype,
                        flags);
        if (fields <= 0) { /* Blank line */
            continue;
        }
        if (fields < 3) {
            fprintf(stderr, "Error reading style file line %d (fields=%d)\n",
                    lineno, fields);
            fclose(in);
            util::exit_nicely();
        }

        //place to keep info about this tag
        temp.name.assign(tag);
        temp.type.assign(datatype);
        temp.flags = parse_tag_flags(flags, lineno);

        // check for special data types, by default everything is handled as text
        //
        // Ignore the special way_area column. It is of type real but we don't really
        // want to convert it back and forth between string and real later. The code
        // will provide a string suitable for the database already.
        if (temp.name != "way_area") {
            auto const typ = tagtypes.find(temp.type);
            if (typ != tagtypes.end()) {
                temp.flags |= typ->second;
            }
        }

        if ((temp.flags != FLAG_DELETE) &&
            ((temp.name.find('?') != std::string::npos) ||
             (temp.name.find('*') != std::string::npos))) {
            fprintf(stderr, "wildcard '%s' in non-delete style entry\n",
                    temp.name.c_str());
            fclose(in);
            util::exit_nicely();
        }

        if ((temp.name == "way_area") && (temp.flags == FLAG_DELETE)) {
            enable_way_area = 0;
        }

        /*    printf("%s %s %d %d\n", temp.name, temp.type, temp.polygon, offset ); */
        bool kept = false;

        //keep this tag info if it applies to nodes
        if (strstr(osmtype, "node")) {
            exlist->add(osmium::item_type::node, temp);
            kept = true;
        }

        //keep this tag info if it applies to ways
        if (strstr(osmtype, "way")) {
            exlist->add(osmium::item_type::way, temp);
            kept = true;
        }

        //do we really want to completely quit on an unusable line?
        if (!kept) {
            fclose(in);
            throw std::runtime_error{
                "Weird style line {}:{}"_format(filename, lineno)};
        }
        num_read++;
    }

    if (ferror(in)) {
        int err = errno;
        fclose(in);
        throw std::runtime_error{"{}: {}"_format(filename, std::strerror(err))};
    }
    fclose(in);
    if (num_read == 0) {
        throw std::runtime_error("Unable to parse any valid columns from "
                                 "the style file. Aborting.");
    }
    return enable_way_area;
}
