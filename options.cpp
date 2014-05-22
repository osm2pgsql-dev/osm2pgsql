#include "options.hpp"

options_t::options_t():
conninfo(NULL), prefix("planet_osm"), scale(DEFAULT_SCALE), projection(new reprojection(PROJ_SPHERE_MERC)), append(0), slim(0),
cache(800), tblsmain_index(NULL), tblsslim_index(NULL), tblsmain_data(NULL), tblsslim_data(NULL), style(OSM2PGSQL_DATADIR "/default.style"),
expire_tiles_zoom(-1), expire_tiles_zoom_min(-1), expire_tiles_filename("dirty_tiles"), enable_hstore(HSTORE_NONE), enable_hstore_index(0),
enable_multi(0), hstore_columns(NULL), n_hstore_columns(0), keep_coastlines(0), parallel_indexing(1),
#ifdef __amd64__
alloc_chunkwise(ALLOC_SPARSE | ALLOC_DENSE),
#else
alloc_chunkwise(ALLOC_SPARSE),
#endif
num_procs(1), droptemp(0),  unlogged(0), hstore_match_only(0), flat_node_cache_enabled(0), excludepoly(0), flat_node_file(NULL), tag_transform_script(NULL)
{

}


options_t options_t::parse_options()
{
    options_t options;









    return options;
}
