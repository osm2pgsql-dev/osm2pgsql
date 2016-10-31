#ifndef OSMIUM_DIFF_ITERATOR_HPP
#define OSMIUM_DIFF_ITERATOR_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2016 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <cassert>
#include <iterator>
#include <type_traits>

#include <osmium/osm/diff_object.hpp>

namespace osmium {

    class OSMObject;

    /**
     * An input iterator wrapping any iterator over OSMObjects. When
     * dereferenced it will yield DiffObject objects pointing to the
     * underlying OSMObjects.
     */
    template <typename TBasicIterator>
    class DiffIterator {

        static_assert(std::is_base_of<osmium::OSMObject, typename TBasicIterator::value_type>::value, "TBasicIterator::value_type must derive from osmium::OSMObject");

        TBasicIterator m_prev;
        TBasicIterator m_curr;
        TBasicIterator m_next;

        const TBasicIterator m_end;

        mutable osmium::DiffObject m_diff;

        void set_diff() const noexcept {
            assert(m_curr != m_end);

            bool use_curr_for_prev =                    m_prev->type() != m_curr->type() || m_prev->id() != m_curr->id();
            bool use_curr_for_next = m_next == m_end || m_next->type() != m_curr->type() || m_next->id() != m_curr->id();

            m_diff = std::move(osmium::DiffObject{
                *(use_curr_for_prev ? m_curr : m_prev),
                *m_curr,
                *(use_curr_for_next ? m_curr : m_next)
            });
        }

    public:

        using iterator_category = std::input_iterator_tag;
        using value_type        = const osmium::DiffObject;
        using difference_type   = std::ptrdiff_t;
        using pointer           = value_type*;
        using reference         = value_type&;

        DiffIterator(TBasicIterator begin, TBasicIterator end) :
            m_prev(begin),
            m_curr(begin),
            m_next(begin == end ? begin : ++begin),
            m_end(std::move(end)),
            m_diff() {
        }

        DiffIterator& operator++() {
            m_prev = std::move(m_curr);
            m_curr = m_next;

            if (m_next != m_end) {
                ++m_next;
            }

            return *this;
        }

        DiffIterator operator++(int) {
            DiffIterator tmp(*this);
            operator++();
            return tmp;
        }

        bool operator==(const DiffIterator& rhs) const noexcept {
            return m_curr == rhs.m_curr && m_end == rhs.m_end;
        }

        bool operator!=(const DiffIterator& rhs) const noexcept {
            return !(*this == rhs);
        }

        reference operator*() const noexcept {
            set_diff();
            return m_diff;
        }

        pointer operator->() const noexcept {
            set_diff();
            return &m_diff;
        }

    }; // class DiffIterator

    /**
     * Create a DiffIterator based on the given iterators.
     */
    template <typename TBasicIterator>
    inline DiffIterator<TBasicIterator> make_diff_iterator(TBasicIterator begin,
                                                           TBasicIterator end) {
        return DiffIterator<TBasicIterator>{begin, end};
    }

} // namespace osmium

#endif // OSMIUM_DIFF_ITERATOR_HPP
