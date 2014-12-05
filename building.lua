tags = { 'building', 'shop', 'amenity' }

function filter_tags_generic(keyvalues, nokeys)
   filter = 0
   tagcount = 0

   --if there were no tags passed in, ie keyvalues is empty
   if nokeys == 0 then
      filter = 1
      return filter, keyvalues
   end

   --remove anything we dont care about
   for i,k in ipairs(tags) do
      if keyvalues[k] then
         tagcount = tagcount + 1
      end
   end

   --if we didnt find any tags we care about
   if tagcount == 0 then
      filter = 1
   end

   --tell the caller whether we think we want this feature or not and give back the modified tags
   return filter, keyvalues
end

function nodes_proc (keyvalues, nokeys)
   --we dont care about nodes at all so filter all of them
   filter = 1
   return filter, keyvalues
end

function rels_proc (keyvalues, nokeys)
   --let the generic filtering do its job
   filter, keyvalues = filter_tags_generic(keyvalues, nokeys)
   if filter == 1 then
      return filter, keyvalues
   end

   --dont keep any relations with types other than multipolygon
   if keyvalues["type"] ~= "multipolygon" then
      filter = 1
      return filter, keyvalues
   end

   --let the caller know if its a keeper or not and give back the modified tags
   return filter, keyvalues
end

function ways_proc (keyvalues, nokeys)
   filter = 0

   --let the generic filtering do its job
   filter, keyvalues = filter_tags_generic(keyvalues, nokeys)
   poly = (filter + 1) % 2
   roads = 0

   --let the caller know if its a keeper or not and give back the  modified tags
   --also tell it whether or not its a polygon or road
   return filter, keyvalues, poly, roads
end

function rel_members_proc (keyvalues, keyvaluemembers, roles, membercount)
   
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
   type = keyvalues["type"]
   keyvalues["type"] = nil

   if (type == "multipolygon") and keyvalues["boundary"] == nil then
      --check if this relation has tags we care about
      polygon = 1
      filter, keyvalues = filter_tags_generic(keyvalues, 1)

      --if the relation didn't have the tags we need go grab the tags from
      --any members that are marked as outers of the multipolygon
      if (filter == 1) then
         for i = 1,membercount do
            if (roles[i] == "outer") then
               for j,k in ipairs(tags) do
                  v = keyvaluemembers[i][k]
                  if v then
                     keyvalues[k] = v
                     filter = 0
                  end
               end
            end
         end
      end
      if filter == 1 then
         return filter, keyvalues, membersuperseeded, boundary, polygon, roads
      end

      --for each tag of each member if the relation have the tag or has a non matching value for it
      --then we say the member will not be used in the relation and is there for not superseeded
      --ie it is kept as a standalone way 
      for i = 1,membercount do
         superseeded = 1
         for k,v in pairs(keyvaluemembers[i]) do
            if ((keyvalues[k] == nil) or (keyvalues[k] ~= v)) then
                superseeded = 0;
                break
            end
         end
         membersuperseeded[i] = superseeded
      end
   end

   return filter, keyvalues, membersuperseeded, boundary, polygon, roads
end
