
-- fake "osm2pgsql" table for test, usually created by the main C++ program
osm2pgsql = {}

-- load the init.lua script that is normally run by the main C++ program
package.path = '../src/?.lua'
require('init')

print("Running Lua tests...")

local o = osm2pgsql

-- ---------------------------------------------------------------------------

-- has_prefix()

assert(o.has_prefix('addr:city', 'addr:'))
assert(not o.has_prefix('addr:city', 'foo'))
assert(o.has_prefix('addr:city', ''))
assert(not o.has_prefix('name', 'addr:'))
assert(o.has_prefix('a', 'a'))
assert(not o.has_prefix('a', 'ab'))
assert(o.has_prefix(nil, 'a') == nil)

-- has_suffix()

assert(o.has_suffix('tiger:source', ':source'))
assert(not o.has_suffix('tiger:source', 'foo'))
assert(o.has_suffix('tiger:source', ''))
assert(not o.has_suffix('name', ':source'))
assert(o.has_suffix('a', 'a'))
assert(not o.has_suffix('a', 'ba'))
assert(o.has_suffix(nil, 'a') == nil)

-- ---------------------------------------------------------------------------

-- clamp
do
    assert(o.clamp(1, 2, 3) == 2)
    assert(o.clamp(3, 1, 4) == 3)
    assert(o.clamp(3, -1, 1) == 1)
    assert(o.clamp(-3, -1, 1) == -1)
    assert(o.clamp(2.718, 0, 3.141) == 2.718)
    assert(o.clamp(nil, -1, 1) == nil)
end

-- make_check_values_func without default
do
    local if_known_highway_value = o.make_check_values_func{
        'motorway', 'trunk', 'primary', 'secondary', 'tertiary'
    }

    assert(if_known_highway_value('motorway') == 'motorway')
    assert(if_known_highway_value('primary') == 'primary')
    assert(if_known_highway_value('residential') == nil)
end

-- make_check_values_func with default
do
    local highway_value = o.make_check_values_func({
        'motorway', 'trunk', 'primary', 'secondary', 'tertiary'
    }, 'road')

    assert(highway_value('motorway') == 'motorway')
    assert(highway_value('primary') == 'primary')
    assert(highway_value('residential') == 'road')
end

-- make_clean_tags_func
do
    local clean_tags = o.make_clean_tags_func{'source',
                                              'source:*',
                                              '*:source',
                                              'note'}

    local tags = {
        source = 'foo',
        highway = 'residential',
        ['source:url'] = 'bar',
        ['tiger:source'] = 'value',
        ['source:vs:source'] = 'removeme',
        ['with:source:infix'] = 'keepme',
        NOTE = 'x'
    }

    assert(clean_tags(tags) == false)

    assert(tags.highway == 'residential')
    assert(tags.NOTE == 'x')
    assert(tags.source == nil)
    assert(tags['source:url'] == nil)
    assert(tags['tiger:source'] == nil)
    assert(tags['source:vs:source'] == nil)
    assert(tags['with:source:infix'] == 'keepme')

    num = 0
    for k, v in pairs(tags) do
        num = num + 1
    end

    assert(num == 3)
end

-- trim
assert(osm2pgsql.trim('') == '')
assert(osm2pgsql.trim(' ') == '')
assert(osm2pgsql.trim('  ') == '')
assert(osm2pgsql.trim('a') == 'a')
assert(osm2pgsql.trim(' a') == 'a')
assert(osm2pgsql.trim('a ') == 'a')
assert(osm2pgsql.trim(' a ') == 'a')
assert(osm2pgsql.trim('  a  ') == 'a')
assert(osm2pgsql.trim('  ab cd  ') == 'ab cd')
assert(osm2pgsql.trim(' \t\r\n\f\va\000b \r\t\n\f\v') == 'a\000b')
assert(osm2pgsql.trim(nil) == nil)

-- split_unit

v, u = o.split_unit('20m', '')
assert(v == 20 and u == 'm')

v, u = o.split_unit('20 m')
assert(v == 20 and u == 'm')

v, u = o.split_unit('20ft', '')
assert(v == 20 and u == 'ft')

v, u = o.split_unit('23.4 ft', '')
assert(v == 23.4 and u == 'ft')

v, u = o.split_unit('20 ft', 'm')
assert(v == 20 and u == 'ft')

v, u = o.split_unit('20km', 'm')
assert(v == 20 and u == 'km')

v, u = o.split_unit('20')
assert(v == 20 and u == nil)

v, u = o.split_unit('20', 'm')
assert(v == 20 and u == 'm')

v, u = o.split_unit('0', 'm')
assert(v == 0 and u == 'm')

v, u = o.split_unit('-20000', 'leagues')
assert(v == -20000 and u == 'leagues')

v, u = o.split_unit('20xx20', '')
assert(v == nil)

v, u = o.split_unit('20-20', '')
assert(v == nil)

v, u = o.split_unit('20xx20', 'foo')
assert(v == nil)

v, u = o.split_unit('abc', 'def')
assert(v == nil)

v, u = o.split_unit(nil)
assert(v == nil and u == nil)

v, u = o.split_unit(nil, 'foo')
assert(v == nil and u == nil)

-- split_string

r = o.split_string('ab c;d;e f;ghi')
assert(#r == 4)
assert(r[1] == 'ab c')
assert(r[2] == 'd')
assert(r[3] == 'e f')
assert(r[4] == 'ghi')

r = o.split_string('ab c;d  ;  e f; ghi')
assert(#r == 4)
assert(r[1] == 'ab c')
assert(r[2] == 'd')
assert(r[3] == 'e f')
assert(r[4] == 'ghi')

r = o.split_string('ab c ')
assert(#r == 1)
assert(r[1] == 'ab c')

r = o.split_string('')
assert(#r == 0)

r = o.split_string('ab c;d  ,  e f, ghi', ',')
assert(#r == 3)
assert(r[1] == 'ab c;d')
assert(r[2] == 'e f')
assert(r[3] == 'ghi')

r = o.split_string(nil)
assert(#r == 0)

-- ---------------------------------------------------------------------------

print("All tests successful")

