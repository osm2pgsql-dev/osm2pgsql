
test = { type = 'geometry', split_at = 'multi' }

dofile(osm2pgsql.config_dir .. '/test_output_flex_multigeom.lua')

