-- This is an example of how to test the example geometries.lua file

-- Utility functions for testing
--- compare two tables.
-- @param t1 A table
-- @param t2 A table
-- @return true or false
function equaltables (t1,t2)
    for k, v in pairs(t1) do
        if t2[k] ~= v then return false end
    end
    for k, v in pairs(t2) do
        if t1[k] ~= v then return false end
    end
    return true
end

-- Before testing we need to mock the supplied osm2pgsql object
osm2pgsql = { srid = 3857}

function osm2pgsql.define_node_table()
end
function osm2pgsql.define_way_table()
end
function osm2pgsql.define_area_table()
end
function osm2pgsql.define_relation_table()
end

require("geometries")

assert(max_length == 100000)

print("TESTING: clean_tags")
tags = {}
assert(clean_tags(tags) == true, "empty tags return")
assert(equaltables(tags, {}), "empty tags modifying")

tags = {odbl = "foo"}
assert(clean_tags(tags) == true, "strips odbl return")
assert(equaltables(tags, {}), "strips odbl modifying")

tags = {created_by = "foo"}
assert(clean_tags(tags) == true, "strips created_by return")
assert(equaltables(tags, {}), "strips created_by modifying")

tags = {source = "foo"}
assert(clean_tags(tags) == true, "strips source return")
assert(equaltables(tags, {}), "strips source modifying")

tags = {["source:ref"] = "foo"}
assert(clean_tags(tags) == true, "strips source:ref return")
assert(equaltables(tags, {}), "strips source:ref modifying")

tags = {foo = "bar"}
assert(clean_tags(tags) == false, "doesn't strip other tags return")
assert(equaltables(tags, {foo = "bar"}), "doesn't strip other tags modifying")

tags = {foo = "bar", odbl = "baz"}
assert(clean_tags(tags) == false, "mixed tags return")
assert(equaltables(tags, {foo = "bar"}), "mixed tags modifying")

print("TESTING: has_area_tags")
assert(not has_area_tags({}), "no tags")

assert(has_area_tags({area = "yes"}), "explicit area")
assert(not has_area_tags({area = "no"}), "explicit not area")

assert(not has_area_tags({foo = "bar"}), "linear tag")
assert(has_area_tags({foo = "bar", area = "yes"}), "linear tag with explicit area")

assert(has_area_tags({landuse = "bar"}), "area tag")
assert(not has_area_tags({landuse = "bar", area = "no"}), "area tag with explicit area")
