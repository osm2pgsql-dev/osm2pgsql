-- This config example file is released into the Public Domain.
--
-- 'grouped-linemerge' implemented as a PURE FLEX STYLE -- no C++ generalizer
-- strategy, just osm2pgsql.run_sql() in process_gen(). It merges connected
-- lines that share the same grouping columns into single (multi)lines, the
-- equivalent of
--
--   SELECT cols..., (ST_Dump(ST_LineMerge(ST_Collect(geom)))).geom
--     FROM roads GROUP BY cols...
--
-- built once and maintained incrementally on updates, using ordinary tile
-- expiry as the "what changed" signal. A typical use is merging road segments
-- that render identically (same name/ref/highway/layer) so labels and shields
-- sit on the whole road instead of on each OSM way.
--
-- Workflow (same as any gen config -- run osm2pgsql-gen after osm2pgsql):
--   Import:                 osm2pgsql -O flex -S grouped-linemerge.lua DATA.osm.pbf
--   Build merged table:     osm2pgsql-gen -S grouped-linemerge.lua
--   Apply an update:        osm2pgsql -a -O flex -S grouped-linemerge.lua CHANGES.osc.gz
--   Update merged table:    osm2pgsql-gen -a -S grouped-linemerge.lua

-- ---------------------------------------------------------------------------
-- Configuration
-- ---------------------------------------------------------------------------
local SRC    = 'roads'         -- source table (one row per OSM way)
local DEST   = 'roads_merged'  -- destination table (the merged lines)
local EXPIRE = 'exp_roads'     -- tile-expire table (the change signal)
local GEOM   = 'geom'          -- geometry column (same name in src and dest)
local ZOOM   = 18              -- expire zoom; high so the changed regions are small
-- Lines merge only when ALL of these columns are equal (NULLs compare equal):
local GROUP  = { 'name', 'ref', 'highway', 'layer' }
-- Optional pre-filter: lines not matching are excluded entirely.
local WHERE  = 'name IS NOT NULL OR ref IS NOT NULL'

-- ---------------------------------------------------------------------------
-- Build the SQL fragments from the grouping columns (this is the only thing
-- the C++ strategy did that wasn't already plain SQL -- and it's trivial here).
-- ---------------------------------------------------------------------------
local function join(sep, fn)
    local t = {}
    for _, c in ipairs(GROUP) do t[#t + 1] = fn(c) end
    return table.concat(t, sep)
end

local V = {
    src = SRC, dest = DEST, expire = EXPIRE, geom = GEOM, zoom = ZOOM,
    where           = '(' .. WHERE .. ')',
    group_cols      = join(', ',  function(c) return '"' .. c .. '"' end),
    group_cols_l    = join(', ',  function(c) return 'l."' .. c .. '"' end),
    group_cols_gk   = join(', ',  function(c) return '"' .. c .. '" AS "gk_' .. c .. '"' end),
    group_cols_l_gk = join(', ',  function(c) return 'l."' .. c .. '" AS "gk_' .. c .. '"' end),
    group_join      = join(' AND ', function(c) return 'l."' .. c .. '" IS NOT DISTINCT FROM n."gk_' .. c .. '"' end),
    group_join_dn   = join(' AND ', function(c) return 'd."' .. c .. '" IS NOT DISTINCT FROM n."gk_' .. c .. '"' end),
}

local function sql(s)
    return (s:gsub('{([%w_]+)}', function(k)
        local v = V[k]
        if v == nil then error("unknown template key '" .. k .. "'") end
        return tostring(v)
    end))
end

-- ---------------------------------------------------------------------------
-- Tables (used by osm2pgsql on import; registered by osm2pgsql-gen)
-- ---------------------------------------------------------------------------
local exp_roads = osm2pgsql.define_expire_output({ table = EXPIRE, maxzoom = ZOOM })

local roads = osm2pgsql.define_table({
    name = SRC,
    ids = { type = 'way', id_column = 'way_id' },
    columns = {
        { column = 'name', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'layer', type = 'int' },
        -- Any geometry change (add/modify/delete) expires the tiles it covers.
        { column = GEOM, type = 'linestring', not_null = true,
            expire = { { output = exp_roads } } },
    }
})

osm2pgsql.define_table({
    name = DEST,
    columns = {
        { column = 'name', type = 'text' },
        { column = 'ref', type = 'text' },
        { column = 'highway', type = 'text' },
        { column = 'layer', type = 'int' },
        { column = GEOM, type = 'linestring', not_null = true },
    }
})

function osm2pgsql.process_way(object)
    if not object.tags.highway then return end
    roads:insert({
        name = object.tags.name,
        ref = object.tags.ref,
        highway = object.tags.highway,
        layer = tonumber(object.tags.layer),
        geom = object:as_linestring(),
    })
end

-- ---------------------------------------------------------------------------
-- The generalization, in SQL. process_gen() runs in osm2pgsql-gen; it branches
-- on osm2pgsql.mode ('create' for the full build, 'append' for updates).
-- ---------------------------------------------------------------------------
function osm2pgsql.process_gen()
    if osm2pgsql.mode == 'create' then
        -- One global GROUP BY + ST_LineMerge over the whole source table.
        osm2pgsql.run_sql({
            description = 'grouped-linemerge: full rebuild',
            transaction = true,
            sql = {
                sql('TRUNCATE {dest}'),
                sql([[INSERT INTO {dest} ({group_cols}, "{geom}")
 SELECT {group_cols}, (ST_Dump(ST_LineMerge(ST_Collect("{geom}")))).geom
   FROM {src} WHERE {where} GROUP BY {group_cols}]]),
            }
        })
        -- Functional endpoint indexes that make the incremental walk fast.
        osm2pgsql.run_sql({
            description = 'grouped-linemerge: endpoint indexes',
            sql = {
                sql([[CREATE INDEX IF NOT EXISTS "{src}_glm_startpt"
 ON {src} USING btree (ST_StartPoint("{geom}")) WHERE {where}]]),
                sql([[CREATE INDEX IF NOT EXISTS "{src}_glm_endpt"
 ON {src} USING btree (ST_EndPoint("{geom}")) WHERE {where}]]),
                sql('ANALYZE {dest}'),
                sql('CREATE INDEX IF NOT EXISTS "{dest}_glm_startpt" ON {dest} USING btree (ST_StartPoint("{geom}"))'),
                sql('CREATE INDEX IF NOT EXISTS "{dest}_glm_endpt"   ON {dest} USING btree (ST_EndPoint("{geom}"))'),
            }
        })
    else
        -- Incremental: consume the expired tiles, walk each affected connected
        -- component out from there, and re-merge only those. Everything is one
        -- run_sql with transaction=true so the ON COMMIT DROP temp tables
        -- survive across the steps; if_has_rows makes it a no-op when nothing
        -- expired.
        osm2pgsql.run_sql({
            description = 'grouped-linemerge: incremental update',
            transaction = true,
            if_has_rows = sql('SELECT 1 FROM {expire} WHERE zoom = {zoom} LIMIT 1'),
            sql = {
                -- 1. expired tiles -> changed-region envelopes
                sql([[CREATE TEMP TABLE _glm_region ON COMMIT DROP AS
 WITH expired AS (DELETE FROM {expire} WHERE zoom = {zoom} RETURNING x, y)
 SELECT ST_TileEnvelope({zoom}, x, y) AS env FROM expired]]),
                'ANALYZE _glm_region',
                -- 2. walk: seed from lines in the region, flood shared endpoints
                --    within the same grouping key (dedup on (group, point)).
                sql([[CREATE TEMP TABLE _glm_nodes ON COMMIT DROP AS
WITH RECURSIVE
seeds AS (
  SELECT {group_cols_l}, l."{geom}"
    FROM _glm_region r
    JOIN {src} l ON l."{geom}" && r.env AND ST_Intersects(l."{geom}", r.env)
   WHERE {where}
),
nodes AS (
  SELECT b.* FROM (
    SELECT {group_cols_gk}, ST_StartPoint("{geom}") AS pt FROM seeds
    UNION
    SELECT {group_cols_gk}, ST_EndPoint("{geom}") FROM seeds
  ) b
  UNION
  SELECT {group_cols_l_gk},
    CASE WHEN ST_StartPoint(l."{geom}") = n.pt
         THEN ST_EndPoint(l."{geom}") ELSE ST_StartPoint(l."{geom}") END
    FROM nodes n
    JOIN {src} l
      ON {group_join}
     AND ( ST_StartPoint(l."{geom}") = n.pt OR ST_EndPoint(l."{geom}") = n.pt )
     AND {where}
)
SELECT * FROM nodes]]),
                'ANALYZE _glm_nodes',
                -- 3. collect the member lines of those components
                sql([[CREATE TEMP TABLE _glm_ways ON COMMIT DROP AS
SELECT DISTINCT ON (l.ctid) {group_cols_l}, l."{geom}"
  FROM _glm_nodes n
  JOIN {src} l
    ON {group_join}
   AND ( ST_StartPoint(l."{geom}") = n.pt OR ST_EndPoint(l."{geom}") = n.pt )
   AND {where}
 ORDER BY l.ctid]]),
                -- 4a. delete stale outputs by reached node (exact endpoint)
                sql([[DELETE FROM {dest} d USING _glm_nodes n
 WHERE {group_join_dn}
   AND ( ST_StartPoint(d."{geom}") = n.pt OR ST_EndPoint(d."{geom}") = n.pt )]]),
                -- 4b. clean up components that vanished entirely (region pass)
                sql([[DELETE FROM {dest} d USING _glm_region r
 WHERE d."{geom}" && r.env AND ST_Intersects(d."{geom}", r.env)]]),
                -- 5. regenerate the affected components
                sql([[INSERT INTO {dest} ({group_cols}, "{geom}")
 SELECT {group_cols}, (ST_Dump(ST_LineMerge(ST_Collect("{geom}")))).geom
   FROM _glm_ways GROUP BY {group_cols}]]),
            }
        })
    end
end
