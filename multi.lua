-- This is an example Lua transform for a multi style
-- It is not intended for use directly with --tag-transform-script but
-- for use from multi.style.json
--
-- See docs/lua.md and docs/multi.md

-- These are copied from default.style, except for INT-.* which is used
-- internally in some stylesheets

delete_tags = {'INT-.*', 'note', 'note:.*', 'source', 'source_ref', 'source:.*',
               'attribution', 'comment', 'fixme', 'created_by', 'odbl',
               'odbl:note', 'SK53_bulk:load', 'tiger:.*', 'NHD:.*', 'nhd:.*',
               'gnis:.*', 'geobase:.*', 'accuracy:meters', 'sub_sea:type',
               'waterway:type', 'KSJ2:.*', 'yh:.*', 'osak:.*', 'kms:.*', 'ngbe:.*',
               'naptan:.*', 'CLC:.*', '3dshapes:ggmodelk', 'AND_nosr_r',
               'import', 'it:fvg:.*'}

-- A function to determine if the tags make the object "interesting" to the
-- buildings table
function building_interesting (kv)
  return kv["building"] and kv["building"] ~= "no"
end

-- For buildings we're not doing any changes to the tagging, so we don't have
-- to pass in a transformation function
function building_ways (kv, num_keys)
  return generic_ways(building_interesting, kv)
end

function building_rels (kv, num_keys)
  return generic_rels(building_interesting, kv)
end

function builing_rel_members (kv, keyvaluemembers, roles, membercount)
  return generic_rel_members(building_interesting, kv, keyvaluemembers, roles, membercount)
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
  return kv
end
function bus_nodes (kv, num_keys)
  return generic_nodes(bus_interesting, kv, bus_transform)
end

-- Some generic and utility helper functions

-- This little function normalizes a tag to true/false. It turns no or false
-- into false and anything else to true. The result can then be used with a
-- boolean column.
-- 
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

-- This function gets rid of something we don't care about
function drop_all (...)
  return 1, {}
end

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
-- Takes an optional function to process tags. Always says it's a polygon if there's matching tags
function generic_ways (f, kv, t)
  if f(kv) then
    t = t or function (kv) return kv end
    tags = t(preprocess_tags(kv))
    return 0, tags, 1, 0
  else
    return 1, {}, 0, 0
  end
end

-- A generic way to process relations, given a function which determines if tags are interesting
-- The tag transformation work is done in generic_rel_members so we don't need to pass in a t
function generic_rels (f, kv)
  if kv["type"] == "multipolygon" and f(kv) then
    tags = kv
    return 0, tags
  else
    return 1, {}
  end
end

-- Basically taken from style.lua
function generic_rel_members (f, keyvals, keyvaluemembers, roles, membercount, t)
  filter = 0
  boundary = 0
  polygon = 0
  roads = 0
  t = t or function (kv) return kv end

  --mark each way of the relation to tell the caller if its going
  --to be used in the relation or by itself as its own standalone way
  --we start by assuming each way will not be used as part of the relation
  membersuperseeded = {}
  for i = 1, membercount do
    membersuperseeded[i] = 0
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
      return filter, tags, membersuperseeded, boundary, polygon, roads
    end

    --for each tag of each member if the relation have the tag or has a non matching value for it
    --then we say the member will not be used in the relation and is there for not superseeded
    --ie it is kept as a standalone way
    for i = 1,membercount do
      superseeded = 1
      for k,v in pairs(keyvaluemembers[i]) do
        if ((keyvals[k] == nil) or (keyvals[k] ~= v)) then
          superseeded = 0;
          break
        end
      end
      membersuperseeded[i] = superseeded
    end
  end
  tags =  t(keyvals)
  return filter, tags, membersuperseeded, boundary, polygon, roads
end