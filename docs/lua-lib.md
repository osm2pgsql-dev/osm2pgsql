
# Lua Helper Library

Note: All functions described in this section are available on the
[`flex`](flex.md) backend only. They cannot be used in
[Lua tag transformations](lua.md).

## `clamp`

Synopsis: `osm2pgsql.clamp(VALUE, MIN, MAX)`

Description: Return VALUE if it is between MIN and MAX, MIN if it is smaller,
or MAX if it is larger. All parameters must be numbers.

Example: `osm2pgsql.clamp(2, 3, 4)` ⟶ `3`

## `make_check_values_func`

Synopsis: `osm2pgsql.make_check_values_func(VALUES[, DEFAULT])`

Description: Return a function that will check its only argument against
the list of VALUES. If it is in that list, it will be returned, otherwise
the DEFAULT (or nil) will be returned.

Example:

```
local get_highway_value = osm2pgsql.make_check_values_func({
        'motorway', 'trunk', 'primary', 'secondary', 'tertiary'
    }, 'road')

...
if object.tags.highway then
    local highway_type = get_highway_value(object.tags.highway)
    ...
end
```

## `mark_member_ways`

Synopsis: `osm2pgsql.mark_member_ways(RELATION)`

Description: Mark all way members of the specified RELATION (for stage 2
processing).

Example:

```
function osm2pgsql.check_relation(object)
    if object.tags.type == 'route' then
        osm2pgsql.mark_member_ways(object)
    end
end
```

## `make_clean_tags_func`

Synopsis: `osm2pgsql.make_clean_tags_func(KEYS)`

Description: Return a function that will remove all tags (in place) from its
only argument if the key matches KEYS. KEYS is an array containing keys or key
prefixes (ending in `*`). The generated function will return `true` if it
removed all tags, `false` if there are still tags left.

Example:

```
local clean_tags = osm2pgsql.make_clean_tags_func{'source', 'source:*', 'note'}

function osm2pgsql.process_node(node)
    if clean_tags(node.tags) then
        return
    end
    ...
end
```
