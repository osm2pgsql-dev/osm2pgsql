function add_z_order(keyvalues)
   z_order = 0
   if (keyvalues["layer"] ~= nil ) then
      z_order = 10*keyvalues["layer"]
   end
   if (keyvalues["railway"] ~= nil) then
      roads = 1
      z_order = z_order + 5
   end

   if (keyvalues["boundary"] == "administrative") then
      roads = 1
   end

   if ((keyvalues["bridge"] == "yes") or (keyvalues["bridge"] == "true") or (keyvalues["bridge"] == 1)) then
      z_order = z_order + 10
   end

   if ((keyvalues["tunnel"] == "yes") or (keyvalues["tunnel"] == "true") or (keyvalues["tunnel"] == 1)) then
      z_order = z_order - 10
   end

   if ((keyvalues["highway"] == "minor") or (keyvalues["highway"] == "road") or (keyvalues["highway"] == "unclassidied") or (keyvalues["highway"] == "residential")) then
      z_order = z_order + 3
   end
   
   if ((keyvalues["highway"] == "tertiary_link") or (keyvalues["highway"] == "tertiary")) then
      z_order = z_order + 4
   end

   if ((keyvalues["highway"] == "secondary") or (keyvalues["highway"] == "secondary_link")) then
      z_order = z_order + 6
      roads = 1
   end

   if ((keyvalues["highway"] == "primary") or (keyvalues["highway"] == "primary_link")) then
      z_order = z_order + 7
      roads = 1
   end

   if ((keyvalues["highway"] == "trunk") or (keyvalues["highway"] == "trunk_link")) then
      z_order = z_order + 8
      roads = 1
   end

   if ((keyvalues["highway"] == "motorway") or (keyvalues["highway"] == "motorway_link")) then
      z_order = z_order + 9
      roads = 1
   end

   keyvalues["z_order"] = z_order

   return keyvalues, roads

end

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
   roads = 0
   
   for k,v in pairs(keyvalues) do tagcount = tagcount + 1; end
   if tagcount == 0 then
      filter = 1
      return filter, keyvalues, poly, roads
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

   keyvalues, roads = add_z_order(keyvalues)


   return filter, keyvalues, poly, roads
end

function filter_tags_relation_member (keyvalues, keyvaluemembers, roles, membercount)
   
   filter = 0
   boundary = 0
   polygon = 0
   roads = 0
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

   keyvalues, roads = add_z_order(keyvalues)

   return filter, keyvalues, membersuperseeded, boundary, polygon, roads
end
