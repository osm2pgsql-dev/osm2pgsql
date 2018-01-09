#ifndef OSMIUM_OSM_CHANGESET_HPP
#define OSMIUM_OSM_CHANGESET_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2017 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <cstdint>
#include <cstring>
#include <iterator>

#include <osmium/memory/collection.hpp>
#include <osmium/memory/item.hpp>
#include <osmium/osm/box.hpp>
#include <osmium/osm/entity.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/timestamp.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/osm/types_from_string.hpp>

namespace osmium {

    namespace builder {
        class ChangesetDiscussionBuilder;
        class ChangesetBuilder;
    } // namespace builder

    class Changeset;

    class ChangesetComment : public osmium::memory::detail::ItemHelper {

        friend class osmium::builder::ChangesetDiscussionBuilder;

        osmium::Timestamp m_date;
        osmium::user_id_type m_uid = 0;
        changeset_comment_size_type m_text_size;
        string_size_type m_user_size;

        ChangesetComment(const ChangesetComment&) = delete;
        ChangesetComment(ChangesetComment&&) = delete;

        ChangesetComment& operator=(const ChangesetComment&) = delete;
        ChangesetComment& operator=(ChangesetComment&&) = delete;

        unsigned char* endpos() {
            return data() + osmium::memory::padded_length(sizeof(ChangesetComment) + m_user_size + m_text_size);
        }

        const unsigned char* endpos() const {
            return data() + osmium::memory::padded_length(sizeof(ChangesetComment) + m_user_size + m_text_size);
        }

        template <typename TMember>
        friend class osmium::memory::CollectionIterator;

        unsigned char* next() {
            return endpos();
        }

        unsigned const char* next() const {
            return endpos();
        }

        void set_user_size(string_size_type size) noexcept {
            m_user_size = size;
        }

        void set_text_size(changeset_comment_size_type size) noexcept {
            m_text_size = size;
        }

    public:

        static constexpr item_type collection_type = item_type::changeset_discussion;

        ChangesetComment(osmium::Timestamp date, osmium::user_id_type uid) noexcept :
            m_date(date),
            m_uid(uid),
            m_text_size(0),
            m_user_size(0) {
        }

        osmium::Timestamp date() const noexcept {
            return m_date;
        }

        osmium::user_id_type uid() const noexcept {
            return m_uid;
        }

        const char* user() const noexcept {
            return reinterpret_cast<const char*>(data() + sizeof(ChangesetComment));
        }

        const char* text() const noexcept {
            return reinterpret_cast<const char*>(data() + sizeof(ChangesetComment) + m_user_size);
        }

    }; // class ChangesetComment

    class ChangesetDiscussion : public osmium::memory::Collection<ChangesetComment, osmium::item_type::changeset_discussion> {

    public:

        ChangesetDiscussion() :
            osmium::memory::Collection<ChangesetComment, osmium::item_type::changeset_discussion>() {
        }

    }; // class ChangesetDiscussion

    static_assert(sizeof(ChangesetDiscussion) % osmium::memory::align_bytes == 0, "Class osmium::ChangesetDiscussion has wrong size to be aligned properly!");

    /**
     * \brief An OSM Changeset, a group of changes made by a single user over
     *        a short period of time.
     *
     * You can not create Changeset objects directly. Use the ChangesetBuilder
     * class to create Changesets in a Buffer.
     */
    class Changeset : public osmium::OSMEntity {

        friend class osmium::builder::ChangesetBuilder;

        osmium::Box       m_bounds;
        osmium::Timestamp m_created_at;
        osmium::Timestamp m_closed_at;
        changeset_id_type m_id = 0;
        num_changes_type  m_num_changes = 0;
        num_comments_type m_num_comments = 0;
        user_id_type      m_uid = 0;
        string_size_type  m_user_size = 0;
        int16_t           m_padding1 = 0;
        int32_t           m_padding2 = 0;

        Changeset() :
            OSMEntity(sizeof(Changeset), osmium::item_type::changeset) {
        }

        void set_user_size(string_size_type size) noexcept {
            m_user_size = size;
        }

        string_size_type user_size() const noexcept {
            return m_user_size;
        }

        unsigned char* subitems_position() {
            return data() + osmium::memory::padded_length(sizeof(Changeset) + m_user_size);
        }

        const unsigned char* subitems_position() const {
            return data() + osmium::memory::padded_length(sizeof(Changeset) + m_user_size);
        }

    public:

        static constexpr osmium::item_type itemtype = osmium::item_type::changeset;

        constexpr static bool is_compatible_to(osmium::item_type t) noexcept {
            return t == itemtype;
        }

        // Dummy to avoid warning because of unused private fields. Do not use.
        int32_t do_not_use() const noexcept {
            return m_padding1 + m_padding2;
        }

        /// Get ID of this changeset
        changeset_id_type id() const noexcept {
            return m_id;
        }

        /**
         * Set ID of this changeset
         *
         * @param id The id.
         * @returns Reference to changeset to make calls chainable.
         */
        Changeset& set_id(changeset_id_type id) noexcept {
            m_id = id;
            return *this;
        }

        /**
         * Set ID of this changeset.
         *
         * @param id The id.
         * @returns Reference to object to make calls chainable.
         */
        Changeset& set_id(const char* id) {
            return set_id(osmium::string_to_changeset_id(id));
        }

        /// Get user id.
        user_id_type uid() const noexcept {
            return m_uid;
        }

        /**
         * Set user id.
         *
         * @param uid The user id.
         * @returns Reference to changeset to make calls chainable.
         */
        Changeset& set_uid(user_id_type uid) noexcept {
            m_uid = uid;
            return *this;
        }

        /**
         * Set user id to given uid or to 0 (anonymous user) if the given
         * uid is smaller than 0.
         *
         * @param uid The user id.
         * @returns Reference to changeset to make calls chainable.
         */
        Changeset& set_uid_from_signed(signed_user_id_type uid) noexcept {
            m_uid = uid < 0 ? 0 : static_cast<user_id_type>(uid);
            return *this;
        }

        /**
         * Set user id to given uid or to 0 (anonymous user) if the given
         * uid is smaller than 0.
         *
         * @returns Reference to changeset to make calls chainable.
         */
        Changeset& set_uid(const char* uid) {
            return set_uid_from_signed(string_to_user_id(uid));
        }

        /// Is this user anonymous?
        bool user_is_anonymous() const noexcept {
            return m_uid == 0;
        }

        /// Get timestamp when this changeset was created.
        osmium::Timestamp created_at() const noexcept {
            return m_created_at;
        }

        /**
         * Get timestamp when this changeset was closed.
         *
         * @returns Timestamp. Will return the empty Timestamp when the
         *          changeset is not yet closed.
         */
        osmium::Timestamp closed_at() const noexcept {
            return m_closed_at;
        }

        /// Is this changeset open?
        bool open() const noexcept {
            return m_closed_at == osmium::Timestamp();
        }

        /// Is this changeset closed?
        bool closed() const noexcept {
            return !open();
        }

        /**
         * Set the timestamp when this changeset was created.
         *
         * @param timestamp Timestamp
         * @returns Reference to changeset to make calls chainable.
         */
        Changeset& set_created_at(const osmium::Timestamp& timestamp) {
            m_created_at = timestamp;
            return *this;
        }

        /**
         * Set the timestamp when this changeset was closed.
         *
         * @param timestamp Timestamp
         * @returns Reference to changeset to make calls chainable.
         */
        Changeset& set_closed_at(const osmium::Timestamp& timestamp) {
            m_closed_at = timestamp;
            return *this;
        }

        /// Get the number of changes in this changeset
        num_changes_type num_changes() const noexcept {
            return m_num_changes;
        }

        /// Set the number of changes in this changeset
        Changeset& set_num_changes(num_changes_type num_changes) noexcept {
            m_num_changes = num_changes;
            return *this;
        }

        /// Set the number of changes in this changeset
        Changeset& set_num_changes(const char* num_changes) {
            return set_num_changes(osmium::string_to_num_changes(num_changes));
        }

        /// Get the number of comments in this changeset
        num_comments_type num_comments() const noexcept {
            return m_num_comments;
        }

        /// Set the number of comments in this changeset
        Changeset& set_num_comments(num_comments_type num_comments) noexcept {
            m_num_comments = num_comments;
            return *this;
        }

        /// Set the number of comments in this changeset
        Changeset& set_num_comments(const char* num_comments) {
            return set_num_comments(osmium::string_to_num_comments(num_comments));
        }

        /**
         * Get the bounding box of this changeset.
         *
         * @returns Bounding box. Can be empty.
         */
        osmium::Box& bounds() noexcept {
            return m_bounds;
        }

        /**
         * Get the bounding box of this changeset.
         *
         * @returns Bounding box. Can be empty.
         */
        const osmium::Box& bounds() const noexcept {
            return m_bounds;
        }

        /// Get user name.
        const char* user() const {
            return reinterpret_cast<const char*>(data() + sizeof(Changeset));
        }

        /// Get the list of tags.
        const TagList& tags() const {
            return osmium::detail::subitem_of_type<const TagList>(cbegin(), cend());
        }

        /**
         * Set named attribute.
         *
         * @param attr Name of the attribute (must be one of "id", "version",
         *             "changeset", "timestamp", "uid", "visible")
         * @param value Value of the attribute
         */
        void set_attribute(const char* attr, const char* value) {
            if (!std::strcmp(attr, "id")) {
                set_id(value);
            } else if (!std::strcmp(attr, "num_changes")) {
                set_num_changes(value);
            } else if (!std::strcmp(attr, "comments_count")) {
                set_num_comments(value);
            } else if (!std::strcmp(attr, "created_at")) {
                set_created_at(osmium::Timestamp(value));
            } else if (!std::strcmp(attr, "closed_at")) {
                set_closed_at(osmium::Timestamp(value));
            } else if (!std::strcmp(attr, "uid")) {
                set_uid(value);
            }
        }

        using iterator       = osmium::memory::CollectionIterator<Item>;
        using const_iterator = osmium::memory::CollectionIterator<const Item>;

        iterator begin() {
            return iterator(subitems_position());
        }

        iterator end() {
            return iterator(data() + padded_size());
        }

        const_iterator cbegin() const {
            return const_iterator(subitems_position());
        }

        const_iterator cend() const {
            return const_iterator(data() + padded_size());
        }

        const_iterator begin() const {
            return cbegin();
        }

        const_iterator end() const {
            return cend();
        }

        ChangesetDiscussion& discussion() {
            return osmium::detail::subitem_of_type<ChangesetDiscussion>(begin(), end());
        }

        const ChangesetDiscussion& discussion() const {
            return osmium::detail::subitem_of_type<const ChangesetDiscussion>(cbegin(), cend());
        }

    }; // class Changeset

    static_assert(sizeof(Changeset) % osmium::memory::align_bytes == 0, "Class osmium::Changeset has wrong size to be aligned properly!");

    /**
     * Changesets are equal if their IDs are equal.
     */
    inline bool operator==(const Changeset& lhs, const Changeset& rhs) {
        return lhs.id() == rhs.id();
    }

    inline bool operator!=(const Changeset& lhs, const Changeset& rhs) {
        return ! (lhs == rhs);
    }

    /**
     * Changesets can be ordered by id.
     */
    inline bool operator<(const Changeset& lhs, const Changeset& rhs) {
        return lhs.id() < rhs.id();
    }

    inline bool operator>(const Changeset& lhs, const Changeset& rhs) {
        return rhs < lhs;
    }

    inline bool operator<=(const Changeset& lhs, const Changeset& rhs) {
        return ! (rhs < lhs);
    }

    inline bool operator>=(const Changeset& lhs, const Changeset& rhs) {
        return ! (lhs < rhs);
    }

} // namespace osmium

#endif // OSMIUM_OSM_CHANGESET_HPP
