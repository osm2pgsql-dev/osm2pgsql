#ifndef OSMIUM_OBJECT_POINTER_COLLECTION_HPP
#define OSMIUM_OBJECT_POINTER_COLLECTION_HPP

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

#include <osmium/handler.hpp>
#include <osmium/osm/object.hpp>

#include <algorithm>
#include <utility>
#include <vector>

// IWYU pragma: no_forward_declare osmium::OSMObject

namespace osmium {

    template <typename TBaseIterator, typename TValue>
    class indirect_iterator : public TBaseIterator {

    public:

        using iterator_category = std::random_access_iterator_tag;
        using value_type        = TValue;
        using difference_type   = std::ptrdiff_t;
        using pointer           = value_type*;
        using reference         = value_type&;

        explicit indirect_iterator(TBaseIterator it) :
            TBaseIterator(it) {
        }

        reference operator*() const noexcept {
            return *TBaseIterator::operator*();
        }

        pointer operator->() const noexcept {
            return &*TBaseIterator::operator*();
        }

    }; // class indirect_iterator

    /**
     * A collection of pointers to OSM objects. The pointers can be easily
     * and quickly sorted or otherwise manipulated, while the objects
     * themselves or the buffers they are in, do not have to be changed.
     *
     * An iterator is provided that can iterate over the pointers but looks
     * like it is iterating over the underlying OSM objects.
     *
     * This class implements the visitor pattern which makes it easy to
     * populate the collection from a buffer of OSM objects:
     *
     *   osmium::ObjectPointerCollection objects;
     *   osmium::memory::Buffer buffer = reader.read();
     *   osmium::apply(buffer, objects);
     *
     * It is not possible to remove pointers from the collection except by
     * clearing the whole collection.
     *
     */
    class ObjectPointerCollection : public osmium::handler::Handler {

        std::vector<osmium::OSMObject*> m_objects;

    public:

        using iterator       = indirect_iterator<std::vector<osmium::OSMObject*>::iterator, osmium::OSMObject>;
        using const_iterator = indirect_iterator<std::vector<osmium::OSMObject*>::const_iterator, const osmium::OSMObject>;

        using ptr_iterator = std::vector<osmium::OSMObject*>::iterator;

        ObjectPointerCollection() = default;

        /**
         * Add a pointer to an object to the collection.
         */
        void osm_object(osmium::OSMObject& object) {
            m_objects.push_back(&object);
        }

        /**
         * Sort objects according to the specified order functor. This function
         * uses a stable sort.
         */
        template <typename TCompare>
        void sort(TCompare&& compare) {
            std::stable_sort(m_objects.begin(), m_objects.end(), std::forward<TCompare>(compare));
        }

        /**
         * Make objects unique according to the specified equality functor.
         *
         * Complexity: Linear in the number of items.
         */
        template <typename TEqual>
        void unique(TEqual&& equal) {
            const auto last = std::unique(m_objects.begin(), m_objects.end(), std::forward<TEqual>(equal));
            m_objects.erase(last, m_objects.end());
        }

        /**
         * Is the collection empty?
         *
         * Complexity: Constant.
         */
        bool empty() const noexcept {
            return m_objects.empty();
        }

        /**
         * Return size of the collection.
         *
         * Complexity: Constant.
         */
        std::size_t size() const noexcept {
            return m_objects.size();
        }

        /// Clear the collection,
        void clear() {
            m_objects.clear();
        }

        iterator begin() {
            return iterator{m_objects.begin()};
        }

        iterator end() {
            return iterator{m_objects.end()};
        }

        const_iterator cbegin() const {
            return const_iterator{m_objects.cbegin()};
        }

        const_iterator cend() const {
            return const_iterator{m_objects.cend()};
        }

        /// Access to begin of pointer vector.
        ptr_iterator ptr_begin() noexcept {
            return m_objects.begin();
        }

        /// Access to end of pointer vector.
        ptr_iterator ptr_end() noexcept {
            return m_objects.end();
        }

    }; // class ObjectPointerCollection

} // namespace osmium

#endif // OSMIUM_OBJECT_POINTER_COLLECTION_HPP
