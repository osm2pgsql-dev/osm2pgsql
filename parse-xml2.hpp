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

#ifndef PARSE_XML2_H
#define PARSE_XML2_H

/* Open and incrementally read an XML file.
 *
 * Parameters:
 *  - filename: the path to the XML file to stream.
 *  - sanitize: if non-zero, use a parser which attempts to sanitize bad UTF-8
 *      characters.
 *  - osmdata: as the file is parsed, it will call functions on `osmdata->out`
 *      based on the action and type of object, e.g: `node_add`, `relation_modify`,
 *      and so on.
 *
 * Return value: 0 on success. A non-zero value indicates failure.
 */
int streamFileXML2(const char *filename, int sanitize, struct osmdata_t *osmdata);

#endif
