#ifndef OSMIUM_IO_DETAIL_QUEUE_UTIL_HPP
#define OSMIUM_IO_DETAIL_QUEUE_UTIL_HPP

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

#include <osmium/memory/buffer.hpp>
#include <osmium/thread/queue.hpp>

#include <cassert>
#include <exception>
#include <future>
#include <string>
#include <utility>

namespace osmium {

    namespace io {

        namespace detail {

            template <typename T>
            using future_queue_type = osmium::thread::Queue<std::future<T>>;

            /**
             * This type of queue contains buffers with OSM data in them.
             * The "end of file" is marked by an invalid Buffer.
             * The buffers are wrapped in a std::future so that they can also
             * transport exceptions. The future also helps with keeping the
             * data in order.
             */
            using future_buffer_queue_type = future_queue_type<osmium::memory::Buffer>;

            /**
             * This type of queue contains OSM file data in the form it is
             * stored on disk, ie encoded as XML, PBF, etc.
             * The "end of file" is marked by an empty string.
             * The strings are wrapped in a std::future so that they can also
             * transport exceptions. The future also helps with keeping the
             * data in order.
             */
            using future_string_queue_type = future_queue_type<std::string>;

            template <typename T>
            inline void add_to_queue(future_queue_type<T>& queue, T&& data) {
                std::promise<T> promise;
                queue.push(promise.get_future());
                promise.set_value(std::forward<T>(data));
            }

            template <typename T>
            inline void add_to_queue(future_queue_type<T>& queue, std::exception_ptr&& exception) {
                std::promise<T> promise;
                queue.push(promise.get_future());
                promise.set_exception(std::move(exception));
            }

            template <typename T>
            inline void add_end_of_data_to_queue(future_queue_type<T>& queue) {
                add_to_queue<T>(queue, T{});
            }

            inline bool at_end_of_data(const std::string& data) noexcept {
                return data.empty();
            }

            inline bool at_end_of_data(const osmium::memory::Buffer& buffer) noexcept {
                return !buffer;
            }

            template <typename T>
            class queue_wrapper {

                future_queue_type<T>& m_queue;

            public:

                explicit queue_wrapper(future_queue_type<T>& queue) :
                    m_queue(queue) {
                }

                queue_wrapper(const queue_wrapper&) = delete;
                queue_wrapper& operator=(const queue_wrapper&) = delete;

                queue_wrapper(queue_wrapper&&) = delete;
                queue_wrapper& operator=(queue_wrapper&&) = delete;

                ~queue_wrapper() noexcept {
                    try {
                        shutdown();
                    } catch (...) { // NOLINT(bugprone-empty-catch)
                        // Ignore any exceptions because destructor must not throw.
                    }
                }

                void shutdown() {
                    m_queue.shutdown();
                }

                bool has_reached_end_of_data() const noexcept {
                    return !m_queue.in_use();
                }

                T pop() {
                    T data;
                    if (m_queue.in_use()) {
                        std::future<T> data_future;
                        m_queue.wait_and_pop(data_future);
                        if (data_future.valid()) {
                            data = std::move(data_future.get());
                            if (at_end_of_data(data)) {
                                m_queue.shutdown();
                            }
                        }
                    }
                    return data;
                }

            }; // class queue_wrapper

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_QUEUE_UTIL_HPP
