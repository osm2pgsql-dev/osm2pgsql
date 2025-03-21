#ifndef OSMIUM_AREA_ASSEMBLER_HPP
#define OSMIUM_AREA_ASSEMBLER_HPP

/*

This file is part of Osmium (https://osmcode.org/libosmium).

Copyright 2013-2025 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <osmium/area/assembler_config.hpp>
#include <osmium/area/detail/basic_assembler_with_tags.hpp>
#include <osmium/area/detail/segment_list.hpp>
#include <osmium/area/problem_reporter.hpp>
#include <osmium/area/stats.hpp>
#include <osmium/builder/osm_object_builder.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/node_ref.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/way.hpp>

#include <cassert>
#include <iostream>
#include <vector>

namespace osmium {

    namespace area {

        /**
         * Assembles area objects from closed ways or multipolygon relations
         * and their members.
         */
        class Assembler : public detail::BasicAssemblerWithTags {

            bool create_area(osmium::memory::Buffer& out_buffer, const osmium::Way& way) {
                osmium::builder::AreaBuilder builder{out_buffer};
                builder.initialize_from_object(way);

                const bool area_okay = create_rings();
                if (area_okay || config().create_empty_areas) {
                    builder.add_item(way.tags());
                }
                if (area_okay) {
                    add_rings_to_area(builder);
                }

                if (report_ways()) {
                    config().problem_reporter->report_way(way);
                }

                return area_okay || config().create_empty_areas;
            }

            bool create_area(osmium::memory::Buffer& out_buffer, const osmium::Relation& relation, const std::vector<const osmium::Way*>& members) {
                set_num_members(members.size());
                osmium::builder::AreaBuilder builder{out_buffer};
                builder.initialize_from_object(relation);

                const bool area_okay = create_rings();
                if (area_okay || config().create_empty_areas) {
                    if (config().keep_type_tag) {
                        builder.add_item(relation.tags());
                    } else {
                        copy_tags_without_type(builder, relation.tags());
                    }
                }
                if (area_okay) {
                    add_rings_to_area(builder);
                }

                if (report_ways()) {
                    for (const osmium::Way* way : members) {
                        config().problem_reporter->report_way(*way);
                    }
                }

                return area_okay || config().create_empty_areas;
            }

        public:

            explicit Assembler(const config_type& config) :
                detail::BasicAssemblerWithTags(config) {
            }

            /**
             * Assemble an area from the given way.
             * The resulting area is put into the out_buffer.
             *
             * @returns false if there was some kind of error building the
             *          area, true otherwise.
             */
            bool operator()(const osmium::Way& way, osmium::memory::Buffer& out_buffer) {
                if (!config().create_way_polygons) {
                    return true;
                }

                if (config().problem_reporter) {
                    config().problem_reporter->set_object(osmium::item_type::way, way.id());
                    config().problem_reporter->set_nodes(way.nodes().size());
                }

                // Ignore (but count) ways without segments.
                if (way.nodes().size() < 2) {
                    ++stats().short_ways;
                    return false;
                }

                if (!way.ends_have_same_id()) {
                    ++stats().duplicate_nodes;
                    if (config().problem_reporter) {
                        config().problem_reporter->report_duplicate_node(way.nodes().front().ref(), way.nodes().back().ref(), way.nodes().front().location());
                    }
                }

                ++stats().from_ways;
                stats().invalid_locations = segment_list().extract_segments_from_way(config().problem_reporter,
                                                                                     stats().duplicate_nodes,
                                                                                     way);
                if (!config().ignore_invalid_locations && stats().invalid_locations > 0) {
                    return false;
                }

                if (config().debug_level > 0) {
                    std::cerr << "\nAssembling way " << way.id() << " containing " << segment_list().size() << " nodes\n";
                }

                // Now create the Area object and add the attributes and tags
                // from the way.
                const bool okay = create_area(out_buffer, way);
                if (okay) {
                    out_buffer.commit();
                } else {
                    out_buffer.rollback();
                }

                if (debug()) {
                    std::cerr << "Done: " << stats() << "\n";
                }

                return okay;
            }

            /**
             * Assemble an area from the given relation and its members.
             * The resulting area is put into the out_buffer.
             *
             * @returns false if there was some kind of error building the
             *          area(s), true otherwise.
             */
            bool operator()(const osmium::Relation& relation, const std::vector<const osmium::Way*>& members, osmium::memory::Buffer& out_buffer) {
                if (!config().create_new_style_polygons) {
                    return true;
                }

                assert(relation.cmembers().size() >= members.size());

                if (config().problem_reporter) {
                    config().problem_reporter->set_object(osmium::item_type::relation, relation.id());
                }

                if (relation.members().empty()) {
                    ++stats().no_way_in_mp_relation;
                    return false;
                }

                ++stats().from_relations;
                stats().invalid_locations = segment_list().extract_segments_from_ways(config().problem_reporter,
                                                                                      stats().duplicate_nodes,
                                                                                      stats().duplicate_ways,
                                                                                      relation,
                                                                                      members);
                if (!config().ignore_invalid_locations && stats().invalid_locations > 0) {
                    return false;
                }
                stats().member_ways = members.size();

                if (stats().member_ways == 1) {
                    ++stats().single_way_in_mp_relation;
                }

                if (config().debug_level > 0) {
                    std::cerr << "\nAssembling relation " << relation.id() << " containing " << members.size() << " way members with " << segment_list().size() << " nodes\n";
                }

                // Now create the Area object and add the attributes and tags
                // from the relation.
                const bool okay = create_area(out_buffer, relation, members);
                if (okay) {
                    out_buffer.commit();
                } else {
                    out_buffer.rollback();
                }

                return okay;
            }

        }; // class Assembler

    } // namespace area

} // namespace osmium

#endif // OSMIUM_AREA_ASSEMBLER_HPP
