#ifndef OSMIUM_IO_DETAIL_OPL_OUTPUT_FORMAT_HPP
#define OSMIUM_IO_DETAIL_OPL_OUTPUT_FORMAT_HPP

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

#include <osmium/io/detail/output_format.hpp>
#include <osmium/io/detail/queue_util.hpp>
#include <osmium/io/detail/string_util.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/file_format.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/memory/collection.hpp>
#include <osmium/memory/item_iterator.hpp>
#include <osmium/osm/box.hpp>
#include <osmium/osm/changeset.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/metadata_options.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/node_ref.hpp>
#include <osmium/osm/object.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/timestamp.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/thread/pool.hpp>
#include <osmium/visitor.hpp>

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace osmium {

    namespace io {

        namespace detail {

            struct opl_output_options {

                /// Which metadata of objects should be added?
                osmium::metadata_options add_metadata;

                /// Should node locations be added to ways?
                bool locations_on_ways = false;

                /// Write in form of a diff file?
                bool format_as_diff = false;

            }; // struct opl_output_options

            /**
             * Writes out one buffer with OSM data in OPL format.
             */
            class OPLOutputBlock : public OutputBlock {

                opl_output_options m_options;

                void append_encoded_string(const char* data) {
                    osmium::io::detail::append_utf8_encoded_string(*m_out, data);
                }

                void write_field_int(char c, int64_t value) {
                    *m_out += c;
                    output_int(value);
                }

                void write_field_timestamp(char c, const osmium::Timestamp& timestamp) {
                    *m_out += c;
                    *m_out += timestamp.to_iso();
                }

                void write_tags(const osmium::TagList& tags) {
                    *m_out += " T";

                    if (tags.empty()) {
                        return;
                    }

                    auto it = tags.begin();
                    append_encoded_string(it->key());
                    *m_out += '=';
                    append_encoded_string(it->value());

                    for (++it; it != tags.end(); ++it) {
                        *m_out += ',';
                        append_encoded_string(it->key());
                        *m_out += '=';
                        append_encoded_string(it->value());
                    }
                }

                void write_meta(const osmium::OSMObject& object) {
                    output_int(object.id());
                    if (m_options.add_metadata.any()) {
                        if (m_options.add_metadata.version()) {
                            *m_out += ' ';
                            write_field_int('v', object.version());
                        }
                        *m_out += " d";
                        *m_out += (object.visible() ? 'V' : 'D');
                        if (m_options.add_metadata.changeset()) {
                            *m_out += ' ';
                            write_field_int('c', object.changeset());
                        }
                        if (m_options.add_metadata.timestamp()) {
                            *m_out += ' ';
                            write_field_timestamp('t', object.timestamp());
                        }
                        if (m_options.add_metadata.uid()) {
                            *m_out += ' ';
                            write_field_int('i', object.uid());
                        }
                        if (m_options.add_metadata.user()) {
                            *m_out += " u";
                            append_encoded_string(object.user());
                        }
                    }
                    write_tags(object.tags());
                }

                void write_location(const osmium::Location& location, const char x, const char y) {
                    const bool not_undefined = !location.is_undefined();
                    *m_out += ' ';
                    *m_out += x;
                    if (not_undefined) {
                        osmium::detail::append_location_coordinate_to_string(std::back_inserter(*m_out), location.x());
                    }
                    *m_out += ' ';
                    *m_out += y;
                    if (not_undefined) {
                        osmium::detail::append_location_coordinate_to_string(std::back_inserter(*m_out), location.y());
                    }
                }

                void write_diff(const osmium::OSMObject& object) {
                    if (m_options.format_as_diff) {
                        *m_out += object.diff_as_char();
                    }
                }

            public:

                OPLOutputBlock(osmium::memory::Buffer&& buffer, const opl_output_options& options) :
                    OutputBlock(std::move(buffer)),
                    m_options(options) {
                }

                std::string operator()() {
                    osmium::apply(m_input_buffer->cbegin(), m_input_buffer->cend(), *this);

                    std::string out;
                    using std::swap;
                    swap(out, *m_out);

                    return out;
                }

                void node(const osmium::Node& node) {
                    write_diff(node);
                    *m_out += 'n';
                    write_meta(node);
                    write_location(node.location(), 'x', 'y');
                    *m_out += '\n';
                }

                void write_field_ref(const osmium::NodeRef& node_ref) {
                    write_field_int('n', node_ref.ref());
                    *m_out += 'x';
                    if (node_ref.location()) {
                        node_ref.location().as_string(std::back_inserter(*m_out), 'y');
                    } else {
                        *m_out += 'y';
                    }
                }

                void way(const osmium::Way& way) {
                    write_diff(way);
                    *m_out += 'w';
                    write_meta(way);

                    *m_out += " N";

                    if (!way.nodes().empty()) {
                        const auto* it = way.nodes().cbegin();
                        if (m_options.locations_on_ways) {
                            write_field_ref(*it);
                            for (++it; it != way.nodes().end(); ++it) {
                                *m_out += ',';
                                write_field_ref(*it);
                            }
                        } else {
                            write_field_int('n', it->ref());
                            for (++it; it != way.nodes().end(); ++it) {
                                *m_out += ',';
                                write_field_int('n', it->ref());
                            }
                        }
                    }

                    *m_out += '\n';
                }

                void relation_member(const osmium::RelationMember& member) {
                    *m_out += item_type_to_char(member.type());
                    output_int(member.ref());
                    *m_out += '@';
                    append_encoded_string(member.role());
                }

                void relation(const osmium::Relation& relation) {
                    write_diff(relation);
                    *m_out += 'r';
                    write_meta(relation);

                    *m_out += " M";

                    if (!relation.members().empty()) {
                        auto it = relation.members().begin();
                        relation_member(*it);
                        for (++it; it != relation.members().end(); ++it) {
                            *m_out += ',';
                            relation_member(*it);
                        }
                    }

                    *m_out += '\n';
                }

                void changeset(const osmium::Changeset& changeset) {
                    write_field_int('c', changeset.id());
                    *m_out += ' ';
                    write_field_int('k', changeset.num_changes());
                    *m_out += ' ';
                    write_field_timestamp('s', changeset.created_at());
                    *m_out += ' ';
                    write_field_timestamp('e', changeset.closed_at());
                    *m_out += ' ';
                    write_field_int('d', changeset.num_comments());
                    *m_out += ' ';
                    write_field_int('i', changeset.uid());
                    *m_out += " u";
                    append_encoded_string(changeset.user());
                    write_location(changeset.bounds().bottom_left(), 'x', 'y');
                    write_location(changeset.bounds().top_right(), 'X', 'Y');
                    write_tags(changeset.tags());
                    *m_out += '\n';
                }

            }; // class OPLOutputBlock

            class OPLOutputFormat : public osmium::io::detail::OutputFormat {

                opl_output_options m_options;

            public:

                OPLOutputFormat(osmium::thread::Pool& pool, const osmium::io::File& file, future_string_queue_type& output_queue) :
                    OutputFormat(pool, output_queue) {
                    m_options.add_metadata      = osmium::metadata_options{file.get("add_metadata")};
                    m_options.locations_on_ways = file.is_true("locations_on_ways");
                    m_options.format_as_diff    = file.is_true("diff");
                }

                void write_buffer(osmium::memory::Buffer&& buffer) final {
                    m_output_queue.push(m_pool.submit(OPLOutputBlock{std::move(buffer), m_options}));
                }

            }; // class OPLOutputFormat

            // we want the register_output_format() function to run, setting
            // the variable is only a side-effect, it will never be used
            const bool registered_opl_output = osmium::io::detail::OutputFormatFactory::instance().register_output_format(osmium::io::file_format::opl,
                [](osmium::thread::Pool& pool, const osmium::io::File& file, future_string_queue_type& output_queue) {
                    return new osmium::io::detail::OPLOutputFormat(pool, file, output_queue);
            });

            // dummy function to silence the unused variable warning from above
            inline bool get_registered_opl_output() noexcept {
                return registered_opl_output;
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_OPL_OUTPUT_FORMAT_HPP
