-- This is an example Lua transform for a multi style
-- It is not intended for use directly with --tag-transform-script but
-- for use from multi.style.json
--
-- See docs/lua.md and docs/multi.md

-- These are copied from default.style
-- If new "tags" are being generated in the Lua code they should normally be
-- added here. This is why name_.* is dropped. In the raw OSM data
-- multi-lingual names are stored in name:*.

delete_tags = {'name_.*', 'note', 'note:.*', 'source', 'source_ref', 'source:.*',
               'attribution', 'comment', 'fixme', 'created_by', 'odbl',
               'odbl:note', 'SK53_bulk:load', 'tiger:.*', 'NHD:.*', 'nhd:.*',
               'gnis:.*', 'geobase:.*', 'accuracy:meters', 'sub_sea:type',
               'waterway:type', 'KSJ2:.*', 'yh:.*', 'osak:.*', 'kms:.*', 'ngbe:.*',
               'naptan:.*', 'CLC:.*', '3dshapes:ggmodelk', 'AND_nosr_r',
               'import', 'it:fvg:.*'}

-- In a real transform the Lua code might be split into multiple files with
-- common code included with "dofile" but a single file is easier for an example

-- A function to determine if the tags make the object "interesting" to the
-- buildings table
function building_interesting (kv)
  return kv["building"] and kv["building"] ~= "no"
end

function building_transform (kv)
  kv["name_en"] = name_lang(kv, "en")
  kv["name_de"] = name_lang(kv, "de")
  kv["name_fr"] = name_lang(kv, "fr")
  return kv
end

-- If we weren't generating multilingual names we could omit building_transform
function building_ways (kv, num_keys)
  return generic_ways(building_interesting, kv, true, building_transform)
end

function building_rels (kv, num_keys)
  return generic_rels(building_interesting, kv)
end

function building_rel_members (kv, keyvaluemembers, roles, membercount)
  return generic_rel_members(building_interesting, kv, keyvaluemembers, roles, membercount, building_transform)
end

-- A function to determine if the tags make the object "interesting" to the
-- bus stop table
function bus_interesting (kv)
  return kv["highway"] == "bus_stop"
end

function bus_transform (kv)
  kv["shelter"] = yesno(kv["shelter"])
  kv["bench"] = yesno(kv["bench"])
  kv["wheelchair"] = yesno(kv["wheelchair"])
  kv["name_en"] = name_lang(kv, "en")
  kv["name_de"] = name_lang(kv, "de")
  kv["name_fr"] = name_lang(kv, "fr")
  return kv
end
function bus_nodes (kv, num_keys)
  return generic_nodes(bus_interesting, kv, bus_transform)
end

-- lookup tables for highways. Using an enum would be better in some ways, but
-- would require creating the type before importing with osm2pgsql, which is
-- not well suited to an example.

highway_lookup = {motorway          = 0,
                  trunk             = 1,
                  primary           = 2,
                  secondary         = 3,
                  tertiary          = 4,
                  unclassified      = 5,
                  residential       = 5}

link_lookup    = {motorway_link     = 0,
                  trunk_link        = 1,
                  primary_link      = 2,
                  secondary_link    = 3,
                  tertiary_link     = 4}

function highway_interesting (kv)
  -- The kv["highway"] check is not necessary but helps performance
  return kv["highway"] and (highway_lookup[kv["highway"]] or link_lookup[kv["highway"]])
end

function highway_transform (kv)
  -- Thanks to highway_interesting we know that kv["highway"] is in one of
  -- highway_lookup or link_lookup
  kv["road_class"] = highway_lookup[kv["highway"]] or link_lookup[kv["highway"]]
  -- This is a lua way of doing an inline conditional
  kv["road_type"] = highway_lookup[kv["highway"]] and "road" or "link"
  kv["name_en"] = name_lang(kv, "en")
  kv["name_de"] = name_lang(kv, "de")
  kv["name_fr"] = name_lang(kv, "fr")
  return kv
end

function highway_ways (kv, num_keys)
  return generic_ways(highway_interesting, kv, false, highway_transform)
end

-- Some generic and utility helper functions

-- This function normalizes a tag to true/false. It turns no or false into
-- false and anything else to true. The result can then be used with a
-- boolean column.
-- > = yesno(nil)
-- nil
-- > = yesno("no")
-- false
-- > = yesno("false")
-- false
-- > = yesno("yes")
-- true
-- > = yesno("foo")
-- true
--
-- A typical usage would be on a tag like bridge, tunnel, or shelter, but not
-- a tag like oneway which could be yes, no, reverse, or unset
function yesno (v)
  -- This is a way of doing an inline condition in Lua
  return v ~= nil and ((v == "no" or v == "false") and "false" or "true") or nil
end

-- Converts a name and name:lang tag into one combined name
-- By passing an optional name_tag parameter it can also work with other
-- multi-lingual tags
function name_lang(kv, lang, name_tag)
  if kv then
    -- Default to the name tag, which is what this will generally be used on.
    name_tag = name_tag or "name"
    -- Defaulting to en is a bit of complete Anglo-centrism
    lang = lang or "en"
    name = kv[name_tag]
    name_trans = kv[name_tag .. ":" .. lang]
    -- If we don't have a translated name, just use the name (which may be blank)
    if not name_trans then return name end
    -- If we do have a translated name and not a local language name, use the translated
    if not name then return name_trans end
    -- if they're the same, return one of them
    if name == name_trans then return name end
    -- This method presents some problems when multiple names get put in the
    -- name tag.
    return name_trans .. "(" .. name .. ")"
  end
end

-- This function gets rid of an object we don't care about
function drop_all (...)
  return 1, {}
end

-- This eliminates tags to be deleted
function preprocess_tags (kv)
  tags = {}
  for k, v in pairs (kv) do
    match = false
    for _, d in ipairs(delete_tags) do
      match = match or string.find(k, d)
    end
    if not match then
      tags[k] = v
    end
  end
  return tags
end

-- A generic way to process nodes, given a function which determines if tags are interesting
-- Takes an optional function to process tags
function generic_nodes (f, kv, t)
  if f(kv) then
    t = t or function (kv) return kv end
    tags = t(kv)
    return 0, tags
  else
    return 1, {}
  end
end


-- A generic way to process ways, given a function which determines if tags are interesting
-- Takes an optional function to process tags.
function generic_ways (interesting, kv, area, transform)
  if interesting(kv) then
    t = transform or function (kv) return kv end
    tags = t(preprocess_tags(kv))
    return 0, tags, area and 1 or 0, 0
  else
    return 1, {}, 0, 0
  end
end

-- A generic way to process relations, given a function which determines if
-- tags are interesting. The tag transformation work is done in
-- generic_rel_members so we don't need to pass in a transformation function.
function generic_rels (f, kv)
  if kv["type"] == "multipolygon" and f(kv) then
    tags = kv
    return 0, tags
  else
    return 1, {}
  end
end

-- Basically taken from style.lua, with the potential for a transform added
function generic_rel_members (f, keyvals, keyvaluemembers, roles, membercount, transform)
  filter = 0
  boundary = 0
  polygon = 0
  roads = 0
  t = transform or function (kv) return kv end

  --mark each way of the relation to tell the caller if its going
  --to be used in the relation or by itself as its own standalone way
  --we start by assuming each way will not be used as part of the relation
  membersuperseded = {}
  for i = 1, membercount do
    membersuperseded[i] = 0
  end

  --remember the type on the relation and erase it from the tags
  type = keyvals["type"]
  keyvals["type"] = nil

  if (type == "multipolygon") and keyvals["boundary"] == nil then
    --check if this relation has tags we care about
    polygon = 1
    filter = f(keyvals)

    --if the relation didn't have the tags we need go grab the tags from
    --any members that are marked as outers of the multipolygon
    if (filter == 1) then
      for i = 1,membercount do
        if (roles[i] == "outer") then
          for j,k in ipairs(tags) do
            v = keyvaluemembers[i][k]
            if v then
              keyvals[k] = v
              filter = 0
            end
          end
        end
      end
    end
    if filter == 1 then
      tags =  t(keyvals)
      return filter, tags, membersuperseded, boundary, polygon, roads
    end

    --for each tag of each member if the relation have the tag or has a non matching value for it
    --then we say the member will not be used in the relation and is there for not superseded
    --ie it is kept as a standalone way
    for i = 1,membercount do
      superseded = 1
      for k,v in pairs(keyvaluemembers[i]) do
        if ((keyvals[k] == nil) or (keyvals[k] ~= v)) then
          superseded = 0;
          break
        end
      end
      membersuperseded[i] = superseded
    end
  end
  tags =  t(keyvals)
  return filter, tags, membersuperseded, boundary, polygon, roads
end
