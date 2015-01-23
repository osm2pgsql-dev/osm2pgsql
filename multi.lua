-- This is an example Lua transform for a multi style
-- It is not intended for use directly with --tag-transform-script but
-- for use from multi.style.json
--
-- See docs/lua.md and docs/multi.md

-- A function to determine if the tags make the object "interesting" to the buildings table
function building_interesting (keyvals)
  return keyvals["building"] and keyvals["building"] ~= "no"
end

function building_ways (keyvals, num_keys)
  return generic_ways(building_interesting, keyvals)
end

function building_rels (keyvals, num_keys)
  return generic_rels(building_interesting, keyvals)
end

function builing_rel_members (keyvals, keyvaluemembers, roles, membercount)
  return generic_rel_members(building_interesting, keyvals, keyvaluemembers, roles, membercount)
end

function bus_nodes_proc (keyvals, num_tags)
  if keyvals["highway"] == "bus_stop" then
    tags = keyvals
    -- Turns values into true unless they're no, leaving empty tags as null.
    -- This lets these columns be boolean, vastly simplifying stylesheet
    -- logic
    if tags["shelter"] then
      -- Checks if the value is no or false, then returns a string that can be turned into a boolean
      tags["shelter"] = ((tags["shelter"] ~= "no" and tags["shelter"] ~= "false") and "true" or "false")
    end
    if tags["bench"] then
      tags["bench"] = ((tags["bench"] ~= "no" and tags["bench"] ~= "false") and "true" or "false")
    end
    if tags["wheelchair"] then
      tags["wheelchair"] = ((tags["wheelchair"] ~= "no" and tags["wheelchair"] ~= "false") and "true" or "false")
    end
    return 0, tags
  else
    return 1, {}
  end
end

-- This function gets rid of something we don't care about
function drop_all (...)
  return 1, {}
end

-- A generic way to process ways, given a function which determines if tags are interesting
function generic_ways (f, kv)
  if f(kv) then
    tags = kv
    return 0, tags, 1, 0
  else
    return 1, {}, 0, 0
  end
end

-- A generic way to process relations, given a function which determines if tags are interesting
function generic_rels (f, kv)
  if kv["type"] == "multipolygon" and f(kv) then
    tags = kv
    return 0, tags
  else
    return 1, {}
  end
end

-- Basically taken from style.lua
function generic_rel_members (f, keyvals, keyvaluemembers, roles, membercount)
  filter = 0
  boundary = 0
  polygon = 0
  roads = 0

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
      return filter, keyvals, membersuperseeded, boundary, polygon, roads
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

  return filter, keyvals, membersuperseeded, boundary, polygon, roads
end
