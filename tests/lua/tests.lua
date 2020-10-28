
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

-- has_suffix()

assert(o.has_suffix('tiger:source', ':source'))
assert(not o.has_suffix('tiger:source', 'foo'))
assert(o.has_suffix('tiger:source', ''))
assert(not o.has_suffix('name', ':source'))
assert(o.has_suffix('a', 'a'))
assert(not o.has_suffix('a', 'ba'))

-- ---------------------------------------------------------------------------

-- clamp
do
    assert(o.clamp(1, 2, 3) == 2)
    assert(o.clamp(3, 1, 4) == 3)
    assert(o.clamp(3, -1, 1) == 1)
    assert(o.clamp(-3, -1, 1) == -1)
    assert(o.clamp(2.718, 0, 3.141) == 2.718)
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

-- ---------------------------------------------------------------------------

print("All tests successful")

