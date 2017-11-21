#include "hstore_tags_storage_t.hpp"

#include "osmtypes.hpp"

// Decodes a portion of a hstore literal from postgres */
// Argument should point to beginning of literal, on return points to delimiter */
inline const char *hstore_tags_storage_t::decode_upto(const char *src,
                                                      char *dst) const
{
    while (*src == ' ')
        src++;
    bool quoted = (*src == '"');
    if (quoted)
        src++;

    while (quoted ? (*src != '"')
                  : (*src != ',' && *src != '\t' && *src != '\n')) {
        if (*src == '\\') {
            switch (src[1]) {
            case 'n':
                *dst++ = '\n';
                break;
            case 't':
                *dst++ = '\t';
                break;
            case 'r':
                *dst++ = '\r';
                break;
            case '"':
                *dst++ = '"';
                break;
            case '\\':
                *dst++ = '\\';
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

// TODO! copypasted from table.cpp. Extract to orginal lib.
//create an escaped version of the string for hstore table insert
// Additional \\ generated because COPY syntax.
void hstore_tags_storage_t::escape4hstore(const char *src, std::string &dst,
                                          const bool escape) const
{
    dst.push_back('"');
    for (size_t i = 0; i < strlen(src); ++i) {
        switch (src[i]) {
        case '\\':
            if (escape) {
                dst.append("\\\\");
            }
            dst.append("\\\\");
            break;
        case '"':
            if (escape) {
                dst.append("\\");
            }
            dst.append("\\\"");
            break;
        case '\t':
            if (escape) {
                dst.append("\\");
            }
            dst.append("\\t");
            break;
        case '\r':
            if (escape) {
                dst.append("\\");
            }
            dst.append("\\r");
            break;
        case '\n':
            if (escape) {
                dst.append("\\");
            }
            dst.append("\\n");
            break;
        default:
            dst.push_back(src[i]);
            break;
        }
    }
    dst.push_back('"');
}

void hstore_tags_storage_t::pgsql_parse_tags(
    const char *string, osmium::builder::TagListBuilder &builder) const
{
    if (*string != '"')
        return;

    char key[1024];
    char val[1024];

    while (strlen(string)) {
        if (*string != '"') {
            string++;
            continue;
        }
        string = decode_upto(string, key);
        // Find start of the next string
        while (*++string != '"') {
        }
        string = decode_upto(string, val);
        builder.add_tag(key, val);
        // String points to the comma or end */
        if (*string == ',') {
            string++;
        }
    }
}

// escape means we return '\N' for copy mode, otherwise we return just nullptr
std::string hstore_tags_storage_t::encode_tags(osmium::OSMObject const &obj,
                                               bool attrs, bool escape) const
{
    std::string result; // = "'";
    for (auto const &it : obj.tags()) {
        escape4hstore(it.key(), result, escape);
        result += "=>";
        escape4hstore(it.value(), result, escape);
        result += ',';
    }
    if (attrs) {
        taglist_t extra;
        extra.add_attributes(obj);
        for (auto const &it : extra) {
            escape4hstore(it.key.c_str(), result, escape);
            result += "=>";
            escape4hstore(it.value.c_str(), result, escape);
            result += ',';
        }
    }

    result[result.size() - 1] = ' ';
    return result;
}
