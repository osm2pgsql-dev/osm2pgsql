-- This is an example of how to test the example geometries.lua file

--- Utility function to do a deep compare
-- (C) Anonymous on snippets.luacode.org, MIT license
function deepcompare(t1,t2)
    local ty1 = type(t1)
    local ty2 = type(t2)
    if ty1 ~= ty2 then return false end
    -- non-table types can be directly compared
    if ty1 ~= 'table' and ty2 ~= 'table' then return t1 == t2 end

    for k1,v1 in pairs(t1) do
        local v2 = t2[k1]
        if v2 == nil or not deepcompare(v1,v2) then return false end
    end
    for k2,v2 in pairs(t2) do
        local v1 = t1[k2]
        if v1 == nil or not deepcompare(v1,v2) then return false end
    end
    return true
end

-- Before testing we need to mock the supplied osm2pgsql object
osm2pgsql = { srid = 3857}

-- Mock the table definition functions to store the definition locally so it can be checked later
node_tables = {}
way_tables = {}
area_tables = {}
relation_tables = {}

table_contents = {pois = {}}

function osm2pgsql.define_node_table(name, cols)
    node_tables[name] = cols
    table_contents[name] = {}
    return {add_row = function(self, obj) table.insert(table_contents[name], obj) end}
end
function osm2pgsql.define_way_table(name, cols)
    way_tables[name] = cols
    table_contents[name] = {}
    return {add_row = function(self, obj) table.insert(table_contents[name], obj) end}
end
function osm2pgsql.define_area_table(name, cols)
    area_tables[name] = cols
    table_contents[name] = {}
    return {add_row = function(self, obj) table.insert(table_contents[name], obj) end}
end
function osm2pgsql.define_relation_table(name, cols)
    relation_tables[name] = cols
    table_contents[name] = {}
    return {add_row = function(self, obj) table.insert(table_contents[name], obj) end}
end

require("geometries")

print("TESTING: definitions")
assert(max_length == 100000, "max_length")

assert(deepcompare(node_tables["pois"], {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'point' }}), "poi table columns")

assert(deepcompare(way_tables["ways"], {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'linestring' }}), "ways table columns")

assert(deepcompare(area_tables["polygons"], {
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'geometry' },
    { column = 'area', type = 'area' }}), "polygons table columns")

assert(deepcompare(relation_tables["boundaries"], {
    { column = 'type', type = 'text' },
    { column = 'tags', type = 'hstore' },
    { column = 'geom', type = 'multilinestring' }}), "boundaries table columns")

print("TESTING: clean_tags")
tags = {}
assert(clean_tags(tags) == true, "empty tags return")
assert(deepcompare(tags, {}), "empty tags modifying")

tags = {odbl = "foo"}
assert(clean_tags(tags) == true, "strips odbl return")
assert(deepcompare(tags, {}), "strips odbl modifying")

tags = {created_by = "foo"}
assert(clean_tags(tags) == true, "strips created_by return")
assert(deepcompare(tags, {}), "strips created_by modifying")

tags = {source = "foo"}
assert(clean_tags(tags) == true, "strips source return")
assert(deepcompare(tags, {}), "strips source modifying")

tags = {["source:ref"] = "foo"}
assert(clean_tags(tags) == true, "strips source:ref return")
assert(deepcompare(tags, {}), "strips source:ref modifying")

tags = {foo = "bar"}
assert(clean_tags(tags) == false, "doesn't strip other tags return")
assert(deepcompare(tags, {foo = "bar"}), "doesn't strip other tags modifying")

tags = {foo = "bar", odbl = "baz"}
assert(clean_tags(tags) == false, "mixed tags return")
assert(deepcompare(tags, {foo = "bar"}), "mixed tags modifying")

print("TESTING: has_area_tags")
assert(not has_area_tags({}), "no tags")

assert(has_area_tags({area = "yes"}), "explicit area")
assert(not has_area_tags({area = "no"}), "explicit not area")

assert(not has_area_tags({foo = "bar"}), "linear tag")
assert(has_area_tags({foo = "bar", area = "yes"}), "linear tag with explicit area")

assert(has_area_tags({landuse = "bar"}), "area tag")
assert(not has_area_tags({landuse = "bar", area = "no"}), "area tag with explicit area")

print("TESTING: process_node")
osm2pgsql.process_node({tags = {}})
-- No objects added
assert(deepcompare(table_contents["pois"], {}), "Untagged node pois")

osm2pgsql.process_node({tags = {foo = "bar"}})
assert(deepcompare(table_contents.pois, {{tags = {foo = "bar"}}}), "Tagged node pois")

osm2pgsql.process_node({tags = {baz = "qux", odbl = "yes"}})
assert(deepcompare(table_contents.pois, {{tags = {foo = "bar"}}, {tags = {baz = "qux"}}}), "2nd tagged node poi")
