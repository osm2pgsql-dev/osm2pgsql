#include "jsonb_tags_storage_t.hpp"

#include <boost/format.hpp>
#include <osmtypes.hpp>
typedef boost::format fmt;

// Decodes a portion of an jsonb literal from postgres */
// Argument should point to beginning of literal, on return points to delimiter */
inline const char *jsonb_tags_storage_t::decode_upto(const char *src,
                                                     char *dst) const
{
    while (*src == ' ')
        src++;
    int quoted = (*src == '"');
    if (quoted)
        src++;

    while (quoted ? (*src != '"')
                  : (*src != ',' && *src != '}' && *src != ':')) {
        if (*src == '\\') {
            switch (src[1]) {
            case 'n':
                *dst++ = '\n';
                break;
            case 't':
                *dst++ = '\t';
                break;
            default:
                *dst++ = src[1];
                break;
            }
            src += 2;
        } else
            *dst++ = *src++;
    }
    if (quoted)
        src++;
    *dst = 0;
    return src;
}

std::string jsonb_tags_storage_t::escape_string(std::string const &in,
                                                bool escape) const
{
    std::string result;
    for (char const c : in) {
        switch (c) {
        case '"':
            if (escape)
                result += "\\";
            result += "\\\"";
            break;
        case '\\':
            if (escape)
                result += "\\\\";
            result += "\\\\";
            break;
        case '\n':
            if (escape)
                result += "\\";
            result += "\\n";
            break;
        case '\r':
            if (escape)
                result += "\\";
            result += "\\r";
            break;
        case '\t':
            if (escape)
                result += "\\";
            result += "\\t";
            break;
        default:
            result += c;
            break;
        }
    }
    return result;
}

void jsonb_tags_storage_t::pgsql_parse_tags(
    const char *string, osmium::builder::TagListBuilder &builder) const
{
    if (*string++ != '{')
        return;

    char key[1024];
    char val[1024];

    while (*string != '}') {
        string = decode_upto(string, key);
        // String points to the comma */
        string++;
        string = decode_upto(string, val);
        builder.add_tag(key, val);
        // String points to the comma or closing '}' */
        if (*string == ',') {
            string++;
        }
    }
}

// escape means we return '\N' for copy mode, otherwise we return just nullptr
std::string jsonb_tags_storage_t::encode_tags(osmium::OSMObject const &obj,
                                              bool attrs, bool escape) const
{
    std::string result = "{";
    for (auto const &it : obj.tags()) {
        result += (fmt("\"%1%\":\"%2%\",") % escape_string(it.key(), escape) %
                   escape_string(it.value(), escape))
                      .str();
    }
    if (attrs) {
        taglist_t extra;
        extra.add_attributes(obj);
        for (auto const &it : extra) {
            result += (fmt("\"%1%\": \"%2%\",") % it.key %
                       escape_string(it.value, escape))
                          .str();
        }
    }

    result[result.size() - 1] = '}';
    return result;
}
