/*
#-----------------------------------------------------------------------------
# osm2pgsql - converts planet.osm file into PostgreSQL
# compatible output suitable to be rendered by mapnik
#-----------------------------------------------------------------------------
# Original Python implementation by Artem Pavlenko
# Re-implementation by Jon Burgess, Copyright 2006
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#-----------------------------------------------------------------------------
*/

#include <cstdio>
#include <cstring>
#include <iostream>

#include "middle-ram.hpp"
#include "options.hpp"
#include "output-pgsql.hpp"
#include "reprojection.hpp"

#include "tests/common.hpp"
#include "tests/common-pg.hpp"

static const char* option_params[] = { "osm2pgsql", "-S", "default.style",
                                       "--number-processes", "1",
                                       "foo", "foo", "foo" };
enum params {  DEF_PARAMS = 5 };

static void check_tables(pg::tempdb *db, options_t &options,
                        const std::string &expected)
{
    options.database_options = db->database_options;
    options.slim = false;

    testing::run_osm2pgsql(options, "tests/liechtenstein-2013-08-03.osm.pbf",
                           "pbf");

    db->check_string(expected, "select find_srid('public', 'planet_osm_roads', 'way')");
}

void check_projection(options_t &options, const char *expected)
{
   if (strcmp(options.projection->target_desc(), expected)) {
        std::cerr << "Unexpected projection when no option is given. Got "
                  << options.projection->target_desc() << "\n";
        throw std::runtime_error("Bad projection");
    }

}

static void test_no_options(pg::tempdb *db)
{
    options_t options(DEF_PARAMS + 1, (char **) option_params);
    check_projection(options, "Spherical Mercator");
    check_tables(db, options, "3857");
}

static void test_latlon_option(pg::tempdb *db)
{
    option_params[DEF_PARAMS] = "-l";
    options_t options(DEF_PARAMS + 2, (char **) option_params);
    check_projection(options, "Latlong");
    check_tables(db, options, "4326");
}

static void test_merc_option(pg::tempdb *db)
{
    option_params[DEF_PARAMS] = "-m";
    options_t options(DEF_PARAMS + 2, (char **) option_params);
    check_projection(options, "Spherical Mercator");
    check_tables(db, options, "3857");
}

static void test_e_option(pg::tempdb *db, const char *param,
                          const char *expected_proj)
{
    option_params[DEF_PARAMS] = "-E";
    option_params[DEF_PARAMS + 1] = param;
    options_t options(DEF_PARAMS + 3, (char **) option_params);
    if (expected_proj)
        check_projection(options, expected_proj);
    check_tables(db, options, param);
}



int main()
{
    auto db = pg::tempdb::create_db_or_skip();

    test_no_options(db.get());
    test_latlon_option(db.get());
    test_merc_option(db.get());
    test_e_option(db.get(), "4326", "Latlong");
    test_e_option(db.get(), "3857", "Spherical Mercator");
    test_e_option(db.get(), "32632", nullptr);

    return 0;
}
