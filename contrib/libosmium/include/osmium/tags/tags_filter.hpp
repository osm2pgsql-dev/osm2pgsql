#ifndef OSMIUM_TAGS_TAGS_FILTER_HPP
#define OSMIUM_TAGS_TAGS_FILTER_HPP

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

#include <osmium/osm/tag.hpp>
#include <osmium/tags/matcher.hpp>

#include <utility>
#include <vector>

namespace osmium {

    /**
     * A TagsFilterBase is a list of rules (defined using TagMatchers) to
     * check tags against. The first rule that matches sets the result.
     *
     * Usually you want to use the TagsFilter class, which is simply a
     * specialization with TResult=bool. But TResult can be any class that
     * has a default constructor and a conversion to bool. The class should
     * be small, because it is copied around in some places.
     *
     * Here is an example matching any "highway" tag except "highway=motorway":
     * @code
     * osmium::TagsFilter filter{false};
     * filter.add_rule(false, osmium::TagMatcher{"highway", "motorway"});
     * filter.add_rule(true, osmium::TagMatcher{"highway"});
     *
     * osmium::Tag& tag = ...;
     * bool result = filter(tag);
     * @endcode
     *
     * Use this instead of the old osmium::tags::Filter.
     */
    template <typename TResult>
    class TagsFilterBase {

        std::vector<std::pair<TResult, TagMatcher>> m_rules;
        TResult m_default_result;

    public:

        using iterator = osmium::memory::CollectionFilterIterator<TagsFilterBase, const osmium::Tag>;

        /**
         * Constructor.
         *
         * @param default_result The result the matching function will return
         *                       if none of the rules matched.
         */
        explicit TagsFilterBase(const TResult default_result = TResult{}) :
            m_default_result(default_result) {
        }

        /**
         * Set the default result, the result the matching function will
         * return if none of the rules matched.
         */
        void set_default_result(const TResult default_result) noexcept {
            m_default_result = default_result;
        }

        /**
         * Add a rule to the filter.
         *
         * @param result The result returned when this rule matches.
         * @param matcher The TagMatcher for checking tags.
         * @returns A reference to this filter for chaining.
         */
        TagsFilterBase& add_rule(const TResult result, const TagMatcher& matcher) {
            m_rules.emplace_back(result, matcher);
            return *this;
        }

        /**
         * Add a rule to the filter.
         *
         * @param result The result returned when this rule matches.
         * @param args Arguments to construct a TagMatcher from that is used
         *             for checking tags.
         * @returns A reference to this filter for chaining.
         */
        template <typename... TArgs>
        TagsFilterBase& add_rule(const TResult result, TArgs&&... args) {
            m_rules.emplace_back(result, osmium::TagMatcher{std::forward<TArgs>(args)...});
            return *this;
        }

        /**
         * Matching function. Check the specified tag against the rules.
         *
         * @param tag A tag.
         * @returns The result of the matching rule, or, if none of the rules
         *          matched, the default result.
         */
        TResult operator()(const osmium::Tag& tag) const noexcept {
            for (const auto& rule : m_rules) {
                if (rule.second(tag)) {
                    return rule.first;
                }
            }
            return m_default_result;
        }

        /**
         * Return the number of rules in this filter.
         *
         * Complexity: Constant.
         */
        std::size_t count() const noexcept {
            return m_rules.size();
        }

        /**
         * Is this filter empty, ie are there no rules defined?
         *
         * Complexity: Constant.
         */
        bool empty() const noexcept {
            return m_rules.empty();
        }

    }; // class TagsFilterBase

    using TagsFilter = TagsFilterBase<bool>;

} // namespace osmium


#endif // OSMIUM_TAGS_TAGS_FILTER_HPP
