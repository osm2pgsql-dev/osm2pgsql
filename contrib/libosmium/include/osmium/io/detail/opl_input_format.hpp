#ifndef OSMIUM_IO_DETAIL_OPL_INPUT_FORMAT_HPP
#define OSMIUM_IO_DETAIL_OPL_INPUT_FORMAT_HPP

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

#include <osmium/io/detail/input_format.hpp>
#include <osmium/io/detail/opl_parser_functions.hpp>
#include <osmium/io/file_format.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/thread/util.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace osmium {

    namespace io {

        namespace detail {

            // Feed data coming in blocks line by line to the OPL parser
            // function. This has been broken out of the OPLParser class
            // where it belongs into a standalone template function to be
            // better testable.
            template <typename T>
            void line_by_line(T& worker) {
                std::string rest;

                while (!worker.input_done()) {
                    std::string input{worker.get_input()};
                    std::string::size_type ppos = 0;

                    if (!rest.empty()) {
                        ppos = input.find_first_of("\n\r");
                        if (ppos == std::string::npos) {
                            rest.append(input);
                            continue;
                        }
                        rest.append(input, 0, ppos);
                        if (!rest.empty()) {
                            worker.parse_line(rest.data());
                            rest.clear();
                        }
                        ++ppos;
                    }

                    for (auto pos = input.find_first_of("\n\r", ppos);
                         pos != std::string::npos;
                         pos = input.find_first_of("\n\r", ppos)) {
                        const char* data = &input[ppos];
                        input[pos] = '\0';
                        if (data[0] != '\0') {
                            worker.parse_line(data);
                        }
                        ppos = pos + 1;
                        if (ppos >= input.size()) {
                            break;
                        }
                    }
                    rest.assign(input, ppos, std::string::npos);
                }

                if (!rest.empty()) {
                    worker.parse_line(rest.data());
                }
            }

            class OPLParser final : public ParserWithBuffer {

                uint64_t m_line_count = 0;

            public:

                explicit OPLParser(parser_arguments& args) :
                    ParserWithBuffer(args) {
                    set_header_value(osmium::io::Header{});
                }

                OPLParser(const OPLParser&) = delete;
                OPLParser& operator=(const OPLParser&) = delete;

                OPLParser(OPLParser&&) = delete;
                OPLParser& operator=(OPLParser&&) = delete;

                ~OPLParser() noexcept override = default;

                void parse_line(const char* data) {
                    switch (*data) {
                        case 'n':
                            maybe_new_buffer(osmium::item_type::node);
                            break;
                        case 'w':
                            maybe_new_buffer(osmium::item_type::way);
                            break;
                        case 'r':
                            maybe_new_buffer(osmium::item_type::relation);
                            break;
                        case 'c':
                            maybe_new_buffer(osmium::item_type::way);
                            break;
                    }

                    if (opl_parse_line(m_line_count, data, buffer(), read_types())) {
                        flush_nested_buffer();
                    }
                    ++m_line_count;
                }

                void run() override {
                    osmium::thread::set_thread_name("_osmium_opl_in");

                    line_by_line(*this);

                    flush_final_buffer();
                }

            }; // class OPLParser

            // we want the register_parser() function to run, setting
            // the variable is only a side-effect, it will never be used
            const bool registered_opl_parser = ParserFactory::instance().register_parser(
                file_format::opl,
                [](parser_arguments& args) {
                    return std::unique_ptr<Parser>(new OPLParser{args});
                });

            // dummy function to silence the unused variable warning from above
            inline bool get_registered_opl_parser() noexcept {
                return registered_opl_parser;
            }

        } // namespace detail

    } // namespace io

} // namespace osmium


#endif // OSMIUM_IO_DETAIL_OPL_INPUT_FORMAT_HPP
