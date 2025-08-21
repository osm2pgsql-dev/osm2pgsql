/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "format.hpp"
#include "logging.hpp"
#include "options.hpp"
#include "pgsql-capabilities.hpp"
#include "pgsql-helper.hpp"
#include "table.hpp"
#include "taginfo.hpp"
#include "util.hpp"

table_t::table_t(std::string const &name, std::string type, columns_t columns,
                 hstores_t hstore_columns, int const srid, bool const append,
                 hstore_column hstore_mode,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread,
                 std::string const &schema)
: m_target(std::make_shared<db_target_descr_t>(schema, name, "osm_id")),
  m_type(std::move(type)), m_srid(fmt::to_string(srid)), m_append(append),
  m_hstore_mode(hstore_mode), m_columns(std::move(columns)),
  m_hstore_columns(std::move(hstore_columns)), m_copy(copy_thread)
{
    // if we dont have any columns
    if (m_columns.empty() && m_hstore_mode != hstore_column::all) {
        throw fmt_error("No columns provided for table {}.", name);
    }

    generate_copy_column_list();
}

table_t::table_t(table_t const &other,
                 std::shared_ptr<db_copy_thread_t> const &copy_thread)
: m_connection_params(other.m_connection_params), m_target(other.m_target),
  m_type(other.m_type), m_srid(other.m_srid), m_append(other.m_append),
  m_hstore_mode(other.m_hstore_mode), m_columns(other.m_columns),
  m_hstore_columns(other.m_hstore_columns), m_table_space(other.m_table_space),
  m_copy(copy_thread)
{
    // if the other table has already started, then we want to execute
    // the same stuff to get into the same state. but if it hasn't, then
    // this would be premature.
    if (other.m_db_connection) {
        connect();
        prepare();
    }
}

void table_t::teardown() { m_db_connection.reset(); }

void table_t::sync() { m_copy.sync(); }

void table_t::connect()
{
    m_db_connection =
        std::make_unique<pg_conn_t>(m_connection_params, "out.pgsql");
}

void table_t::start(connection_params_t const &connection_params,
                    std::string const &table_space)
{
    if (m_db_connection) {
        throw fmt_error("{} cannot start, its already started.",
                        m_target->name());
    }

    m_connection_params = connection_params;
    m_table_space = tablespace_clause(table_space);

    connect();
    log_info("Setting up table '{}'", m_target->name());

    // we are making a new table
    if (!m_append) {
        drop_table_if_exists(*m_db_connection, m_target->schema(),
                             m_target->name());
    }

    // These _tmp tables can be left behind if we run out of disk space.
    drop_table_if_exists(*m_db_connection, m_target->schema(),
                         m_target->name() + "_tmp");

    //making a new table
    if (!m_append) {
        //define the new table
        auto const qual_name =
            qualified_name(m_target->schema(), m_target->name());
        auto sql =
            fmt::format("CREATE UNLOGGED TABLE {} (osm_id int8,", qual_name);

        //first with the regular columns
        for (auto const &column : m_columns) {
            check_identifier(column.name, "column names");
            check_identifier(column.type_name, "column types");
            sql += fmt::format(R"("{}" {},)", column.name, column.type_name);
        }

        //then with the hstore columns
        for (auto const &hcolumn : m_hstore_columns) {
            check_identifier(hcolumn, "column names");
            sql += fmt::format(R"("{}" hstore,)", hcolumn);
        }

        //add tags column
        if (m_hstore_mode != hstore_column::none) {
            sql += "\"tags\" hstore,";
        }

        sql += fmt::format("way geometry({},{}) )", m_type, m_srid);

        // The final tables are created with CREATE TABLE AS ... SELECT * FROM ...
        // This means that they won't get this autovacuum setting, so it doesn't
        // doesn't need to be RESET on these tables
        sql += " WITH (autovacuum_enabled = off)";
        //add the main table space
        sql += m_table_space;

        //create the table
        m_db_connection->exec(sql);

        if (m_srid != "4326") {
            create_geom_check_trigger(*m_db_connection, m_target->schema(),
                                      m_target->name(), "ST_IsValid(NEW.way)");
        }
    }

    prepare();
}

void table_t::prepare()
{
    //let postgres cache this query as it will presumably happen a lot
    auto const qual_name = qualified_name(m_target->schema(), m_target->name());
    m_db_connection->prepare(
        "get_wkb", "SELECT way FROM {} WHERE osm_id = $1::int8", qual_name);
}

void table_t::generate_copy_column_list()
{
    util::string_joiner_t joiner{',', '"'};

    joiner.add("osm_id");

    // first with the regular columns
    for (auto const &column : m_columns) {
        joiner.add(column.name);
    }

    // then with the hstore columns
    for (auto const &hcolumn : m_hstore_columns) {
        joiner.add(hcolumn);
    }

    // add tags column
    if (m_hstore_mode != hstore_column::none) {
        joiner.add("tags");
    }

    // add geom column
    joiner.add("way");

    m_target->set_rows(joiner());
}

void table_t::stop(bool updateable, bool enable_hstore_index,
                   std::string const &table_space_index)
{
    // make sure that all data is written to the DB before continuing
    m_copy.sync();

    auto const qual_name = qualified_name(m_target->schema(), m_target->name());
    auto const qual_tmp_name =
        qualified_name(m_target->schema(), m_target->name() + "_tmp");

    if (!m_append) {
        if (m_srid != "4326") {
            drop_geom_check_trigger(*m_db_connection, m_target->schema(),
                                    m_target->name());
        }

        log_info("Clustering table '{}' by geometry...", m_target->name());

        std::string const sql =
            fmt::format("CREATE TABLE {} {} AS SELECT * FROM {} ORDER BY way",
                        qual_tmp_name, m_table_space, qual_name);

        m_db_connection->exec(sql);

        m_db_connection->exec("DROP TABLE {}", qual_name);
        m_db_connection->exec(R"(ALTER TABLE {} RENAME TO "{}")", qual_tmp_name,
                              m_target->name());

        log_info("Creating geometry index on table '{}'...", m_target->name());

        // Use fillfactor 100 for un-updatable imports
        m_db_connection->exec("CREATE INDEX ON {} USING GIST (way) {} {}",
                              qual_name,
                              (updateable ? "" : "WITH (fillfactor = 100)"),
                              tablespace_clause(table_space_index));

        /* slim mode needs this to be able to apply diffs */
        if (updateable) {
            log_info("Creating osm_id index on table '{}'...",
                     m_target->name());
            m_db_connection->exec("CREATE INDEX ON {} USING BTREE (osm_id) {}",
                                  qual_name,
                                  tablespace_clause(table_space_index));
            if (m_srid != "4326") {
                create_geom_check_trigger(*m_db_connection, m_target->schema(),
                                          m_target->name(),
                                          "ST_IsValid(NEW.way)");
            }
        }

        /* Create hstore index if selected */
        if (enable_hstore_index) {
            log_info("Creating hstore indexes on table '{}'...",
                     m_target->name());
            if (m_hstore_mode != hstore_column::none) {
                m_db_connection->exec("CREATE INDEX ON {} USING GIN (tags) {}",
                                      qual_name,
                                      tablespace_clause(table_space_index));
            }
            for (auto const &hcolumn : m_hstore_columns) {
                m_db_connection->exec(
                    R"(CREATE INDEX ON {} USING GIN ("{}") {})", qual_name,
                    hcolumn, tablespace_clause(table_space_index));
            }
        }
        log_info("Analyzing table '{}'...", m_target->name());
        analyze_table(*m_db_connection, m_target->schema(), m_target->name());
    }
    teardown();
}

void table_t::delete_row(osmid_t const id)
{
    m_copy.new_line(m_target);
    m_copy.delete_object(id);
}

void table_t::write_row(osmid_t id, taglist_t const &tags,
                        std::string const &geom)
{
    m_copy.new_line(m_target);

    //add the osm id
    m_copy.add_column(id);

    // used to remember which columns have been written out already.
    std::vector<bool> used;

    if (m_hstore_mode != hstore_column::none) {
        used.assign(tags.size(), false);
    }

    //get the regular columns' values
    write_columns(tags, m_hstore_mode == hstore_column::norm ? &used : nullptr);

    //get the hstore columns' values
    write_hstore_columns(tags);

    //get the key value pairs for the tags column
    if (m_hstore_mode != hstore_column::none) {
        write_tags_column(tags, used);
    }

    //add the geometry - encoding it to hex along the way
    m_copy.add_hex_geom(geom);

    //send all the data to postgres
    m_copy.finish_line();
}

void table_t::write_columns(taglist_t const &tags, std::vector<bool> *used)
{
    for (auto const &column : m_columns) {
        std::size_t const idx = tags.indexof(column.name);
        if (idx != std::numeric_limits<std::size_t>::max()) {
            escape_type(tags[idx].value, column.type);

            // Remember we already used this one so we can't use
            // again later in the hstore column.
            if (used) {
                (*used)[idx] = true;
            }
        } else {
            m_copy.add_null_column();
        }
    }
}

/// Write all tags to hstore. Exclude tags written to other columns and z_order.
void table_t::write_tags_column(taglist_t const &tags,
                                std::vector<bool> const &used)
{
    m_copy.new_hash();

    for (std::size_t i = 0; i < tags.size(); ++i) {
        tag_t const &tag = tags[i];
        if (!used[i] && (tag.key != "z_order")) {
            m_copy.add_hash_elem(tag.key, tag.value);
        }
    }

    m_copy.finish_hash();
}

/* write an hstore column to the database */
void table_t::write_hstore_columns(taglist_t const &tags)
{
    for (auto const &hcolumn : m_hstore_columns) {
        bool added = false;

        for (auto const &tag : tags) {
            //check if the tag's key starts with the name of the hstore column
            if (tag.key.compare(0, hcolumn.size(), hcolumn) == 0) {
                char const *const shortkey = &tag.key[hcolumn.size()];

                //and pack the shortkey with its value into the hstore
                //hstore ASCII representation looks like "key"=>"value"
                if (!added) {
                    added = true;
                    m_copy.new_hash();
                }

                m_copy.add_hash_elem(shortkey, tag.value.c_str());
            }
        }

        if (added) {
            m_copy.finish_hash();
        } else {
            m_copy.add_null_column();
        }
    }
}

void table_t::task_wait()
{
    auto const run_time = m_task_result.wait();
    log_info("All postprocessing on table '{}' done in {}.", m_target->name(),
             util::human_readable_duration(run_time));
}

// NOLINTBEGIN(google-runtime-int,cert-err34-c)
// This is legacy code which will be removed anyway.

/* Escape data appropriate to the type */
void table_t::escape_type(std::string const &value, column_type_t flags)
{
    switch (flags) {
    case column_type_t::INT: {
        // For integers we take the first number, or the average if it's a-b
        long long from = 0;
        long long to = 0;
        // limit number of digits parsed to avoid undefined behaviour in sscanf
        int const items =
            std::sscanf(value.c_str(), "%18lld-%18lld", &from, &to);
        if (items == 1 && from <= std::numeric_limits<int32_t>::max() &&
            from >= std::numeric_limits<int32_t>::min()) {
            m_copy.add_column(from);
        } else if (items == 2) {
            // calculate mean while avoiding overflows
            int64_t const mean =
                (from / 2) + (to / 2) + ((from % 2 + to % 2) / 2);
            if (mean <= std::numeric_limits<int32_t>::max() &&
                mean >= std::numeric_limits<int32_t>::min()) {
                m_copy.add_column(mean);
            } else {
                m_copy.add_null_column();
            }
        } else {
            m_copy.add_null_column();
        }
        break;
    }
    case column_type_t::REAL:
        /* try to "repair" real values as follows:
         * assume "," to be a decimal mark which need to be replaced by "."
         * like int4 take the first number, or the average if it's a-b
         * assume SI unit (meters)
         * convert feet to meters (1 foot = 0.3048 meters)
         * reject anything else
         */
        {
            std::string escaped{value};
            std::replace(escaped.begin(), escaped.end(), ',', '.');

            double from = NAN;
            double to = NAN;
            int const items =
                std::sscanf(escaped.c_str(), "%lf-%lf", &from, &to);
            if (items == 1) {
                if (escaped.size() > 1 &&
                    escaped.substr(escaped.size() - 2) == "ft") {
                    from *= 0.3048;
                }
                m_copy.add_column(from);
            } else if (items == 2) {
                if (escaped.size() > 1 &&
                    escaped.substr(escaped.size() - 2) == "ft") {
                    from *= 0.3048;
                    to *= 0.3048;
                }
                m_copy.add_column((from + to) / 2);
            } else {
                m_copy.add_null_column();
            }
            break;
        }
    case column_type_t::TEXT:
        m_copy.add_column(value);
        break;
    }
}
// NOLINTEND(google-runtime-int,cert-err34-c)

pg_result_t table_t::get_wkb(osmid_t id)
{
    return m_db_connection->exec_prepared_as_binary("get_wkb", id);
}
