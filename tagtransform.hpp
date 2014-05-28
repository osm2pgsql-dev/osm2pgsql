
#ifndef TAGTRANSFORM_H
#define TAGTRANSFORM_H

#include "output.hpp"
#include "taginfo.hpp"

#ifdef HAVE_LUA
extern "C" {
	#include <lua.h>
	#include <lualib.h>
	#include <lauxlib.h>
}
#endif



class tagtransform {
public:
	tagtransform(const options_t *options_);
	~tagtransform();

	unsigned int filter_node_tags(keyval *tags, const export_list *exlist);
	unsigned int filter_way_tags(keyval *tags, int * polygon, int * roads, const export_list *exlist);
	unsigned int filter_rel_tags(keyval *tags, const export_list *exlist);
	unsigned int filter_rel_member_tags(keyval *rel_tags, int member_count,
		keyval *member_tags,const char **member_role, int * member_superseeded,
		int * make_boundary, int * make_polygon, int * roads, const export_list *exlist);

private:
	unsigned int lua_filter_basic_tags(const OsmType type, keyval *tags, int * polygon, int * roads);
	unsigned int c_filter_basic_tags(const OsmType type, keyval *tags, int *polygon, int * roads,
	    const export_list *exlist);
	unsigned int lua_filter_rel_member_tags(keyval *rel_tags, const int member_count,
	        keyval *member_tags,const char **member_role,
	        int * member_superseeded, int * make_boundary, int * make_polygon, int * roads);

	const options_t* options;
	const bool transform_method;
#ifdef HAVE_LUA
	lua_State *L;
#endif

};

#endif //TAGTRANSFORM_H
