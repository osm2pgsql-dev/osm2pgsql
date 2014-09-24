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

#ifndef PARSE_PRIMITIVE_H
#define PARSE_PRIMITIVE_H

#include "parse.hpp"

class parse_primitive_t: public parse_t
{
public:
	parse_primitive_t(const int extra_attributes_, const bool bbox_, const boost::shared_ptr<reprojection>& projection_,
				const double minlon, const double minlat, const double maxlon, const double maxlat);
	virtual ~parse_primitive_t();
	virtual int streamFile(const char *filename, const int sanitize, osmdata_t *osmdata);
protected:
	parse_primitive_t();
	actions_t ParseAction(char **token, int tokens);
	void StartElement(char *name, char *line, struct osmdata_t *osmdata);
	void EndElement(const char *name, struct osmdata_t *osmdata);
	void process(char *line, struct osmdata_t *osmdata);
};

#endif
