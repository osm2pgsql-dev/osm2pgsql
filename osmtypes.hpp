/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H

// when __cplusplus is defined, we need to define this macro as well
// to get the print format specifiers in the inttypes.h header.
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include "config.h"

#include <string>
#include <vector>
#include <cmath>

#include <osmium/builder/attr.hpp>
#include <osmium/geom/coordinates.hpp>
#include <osmium/osm.hpp>

typedef int64_t osmid_t;
#define strtoosmid strtoll
#define PRIdOSMID PRId64
#define POSTGRES_OSMID_TYPE "int8"

struct member {
    osmium::item_type type;
    osmid_t id;
    std::string role;

    operator osmium::builder::attr::member_type const() const
    {
        return osmium::builder::attr::member_type(type, id, role.c_str());
    }

    member(osmium::item_type t, osmid_t i, const std::string &r)
    : type(t), id(i), role(r) {}
};

struct memberlist_t : public std::vector<member> {
    memberlist_t() {}

    explicit memberlist_t(osmium::RelationMemberList const &list) {
        for (auto const &m: list) {
            emplace_back(m.type(), m.ref(), m.role());
        }
    }

    std::vector<osmium::builder::attr::member_type> for_builder() const
    {
        std::vector<osmium::builder::attr::member_type> ret;
        for (auto const &m : *this) {
            ret.emplace_back(m.type, m.id, m.role.c_str());
        }

        return ret;
    }
};

struct tag_t {
  std::string key;
  std::string value;

  operator std::pair<char const *, char const *> const() const
  {
      return std::pair<char const *, char const *>(key.c_str(), value.c_str());
  }

  tag_t(const std::string &k, const std::string &v) : key(k), value(v) {}
};


class taglist_t : public std::vector<tag_t> {

  typedef std::vector<tag_t> base_t;

public:
  taglist_t() {}

  explicit taglist_t(osmium::TagList const &list)
  {
      for (auto const &t : list) {
          emplace_back(t.key(), t.value());
      }
  }

  void add_attributes(const osmium::OSMObject &obj)
  {
      emplace_back("osm_user", obj.user());
      emplace_back("osm_uid", std::to_string(obj.uid()));
      emplace_back("osm_version", std::to_string(obj.version()));
      emplace_back("osm_timestamp", obj.timestamp().to_iso());
      emplace_back("osm_changeset", std::to_string(obj.changeset()));
  }

  const tag_t *find(const std::string &key) const { return _find(key); }

  tag_t *find(const std::string &key) {  return const_cast<tag_t *>(_find(key)); }

  int indexof(const std::string &key) const
  {
      for (size_t i = 0; i < size(); ++i)
          if (at(i).key == key)
              return int(i);

      return -1;
  }

  const std::string *get(const std::string &key) const
  {
    for (base_t::const_iterator it = begin() ; it != end(); ++it)
      if (it->key == key)
        return &(it->value);

    return 0;
  }

  static bool value_to_bool(char const *value, bool defval)
  {
      if (!defval &&
          (strcmp(value, "yes") == 0
           || strcmp(value, "true") == 0
           || strcmp(value, "1") == 0))
          return true;
      if (defval &&
          (strcmp(value, "no") == 0 || strcmp(value, "false") == 0 || strcmp(value, "0") == 0))
          return false;

      return defval;
  }

  bool get_bool(const std::string &key, bool defval) const
  {
      for (auto const &t : *this)
          if (t.key == key) {
              return value_to_bool(t.value.c_str(), defval);
          }

      return defval;
  }

  void push_dedupe(const tag_t& t)
  {
      if (find(t.key) == 0)
          push_back(t);
  }

    /** Pushes a tag onto the list, overriding an existing tag if necessary */
    void push_override(const tag_t& t)
    {
        auto *tag_in_list = find(t.key);

        if (tag_in_list == 0) {
            push_back(t);
        } else {
            tag_in_list->value = t.value;
        }
    }
  bool contains(const std::string &key) const { return _find(key) != 0; }

private:
  const tag_t *_find(const std::string &key) const
  {
    for (base_t::const_iterator it = begin() ; it != end(); ++it)
      if (it->key == key)
        return &(*it);

    return 0;
  }


};

struct idlist_t : public std::vector<osmid_t> {
    idlist_t() {}

    explicit idlist_t(osmium::NodeRefList const &list) {
        for (auto const &n : list) {
            push_back(n.ref());
        }
    }
};

typedef std::vector<char const *> rolelist_t;

#endif
