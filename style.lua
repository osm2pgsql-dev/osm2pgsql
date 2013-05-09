function filter_tags_node (keyvalues, nokeys)
   filter = 0
   tagcount = 0

   keyvalues["FIXME"] = nil
   keyvalues["note"] = nil
   keyvalues["source"] = nil

   for k,v in pairs(keyvalues) do tagcount = tagcount + 1; end
   if tagcount == 0 then
      filter = 1
   end

   return filter, keyvalues
end

function filter_basic_tags_rel (keyvalues, nokeys)
   filter = 0
   tagcount = 0
   for i,v in pairs(keyvalues) do tagcount = tagcount + 1 end
   if tagcount == 0 then
      filter = 1
      return filter, keyvalues
   end

   keyvalues["FIXME"] = nil
   keyvalues["note"] = nil
   keyvalues["source"] = nil

   if ((keyvalues["type"] ~= "route") and (keyvalues["type"] ~= "multipolygon") and (keyvalues["type"] ~= "boundary")) then
      filter = 1
      return filter, keyvalues
   end

   return filter, keyvalues
end

function filter_tags_way (keyvalues, nokeys)
   filter = 0
   poly = 0
   tagcount = 0
   for k,v in pairs(keyvalues) do tagcount = tagcount + 1; end
   if tagcount == 0 then
      filter = 1
      return filter, keyvalues, poly
   end

   keyvalues["FIXME"] = nil
   keyvalues["note"] = nil
   keyvalues["source"] = nil

   if ((keyvalues["building"] ~= nil) or 
       (keyvalues["landuse"] ~= nil) or 
          (keyvalues["amenity"] ~= nil) or 
          (keyvalues["harbour"] ~= nil) or 
          (keyvalues["historic"] ~= nil) or 
          (keyvalues["leisure"] ~= nil) or 
          (keyvalues["man_made"] ~= nil) or 
          (keyvalues["military"] ~= nil) or 
          (keyvalues["natural"] ~= nil) or
          (keyvalues["office"] ~= nil) or 
          (keyvalues["place"] ~= nil) or 
          (keyvalues["power"] ~= nil) or 
          (keyvalues["public_transport"] ~= nil) or 
          (keyvalues["shop"] ~= nil) or 
          (keyvalues["sport"] ~= nil) or 
          (keyvalues["tourism"] ~= nil) or 
          (keyvalues["waterway"] ~= nil) or 
          (keyvalues["wetland"] ~= nil) or 
          (keyvalues["water"] ~= nil) or 
          (keyvalues["aeroway"] ~= nil))
      then
      poly = 1;
   end

   if ((keyvalues["area"] == "yes") or (keyvalues["area"] == "1") or (keyvalues["area"] == "true")) then
      poly = 1;
   elseif ((keyvalues["area"] == "no") or (keyvalues["area"] == "0") or (keyvalues["area"] == "false")) then
      poly = 0;
   end

   return filter, keyvalues, poly
end

function filter_tags_relation_member (keyvalues, keyvaluemembers, roles, membercount)
   
   filter = 0
   boundary = 0
   polygon = 0
   membersuperseeded = {}
   for i = 1, membercount do
      membersuperseeded[i] = 0
   end

   type = keyvalues["type"]
   keyvalues["type"] = nil
  

   if (type == "boundary") then
      boundary = 1
   end
   if ((type == "multipolygon") and keyvalues["boundary"]) then
      boundary = 1
   elseif (type == "multipolygon") then
      polygon = 1
      tagcount = 0;
      for i,v in pairs(keyvalues) do tagcount = tagcount + 1 end
      if ((tagcount == 0) or (tagcount == 1 and keyvalues["name"])) then
         for i = 1,membercount do
            if (roles[i] == "outer") then 
               for k,v in pairs(keyvaluemembers[i]) do
                  keyvalues[k] = v
               end
            end
         end
      end
      for i = 1,membercount do
         superseeded = 1
         for k,v in pairs(keyvaluemembers[i]) do
            if ((keyvalues[k] == nil) or (keyvalues[k] ~= v)) then
               superseeded = 0;
            end
         end
         membersuperseeded[i] = superseeded
      end
   end
   
   return filter, keyvalues, membersuperseeded, boundary, polygon
end
