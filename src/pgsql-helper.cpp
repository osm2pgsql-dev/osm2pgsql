/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "pgsql-helper.hpp"

#include "format.hpp"
#include "pgsql.hpp"
#include "pgsql-capabilities.hpp"

#include <cassert>

idlist_t get_ids_from_result(pg_result_t const &result) {
    assert(result.num_tuples() >= 0);

    idlist_t ids;
    ids.reserve(static_cast<std::size_t>(result.num_tuples()));

    for (int i = 0; i < result.num_tuples(); ++i) {
        ids.push_back(osmium::string_to_object_id(result.get_value(i, 0)));
    }

    return ids;
}

void create_geom_check_trigger(pg_conn_t const &db_connection,
                               std::string const &schema,
                               std::string const &table,
                               std::string const &condition)
{
    std::string const func_name =
        qualified_name(schema, table + "_osm2pgsql_valid");

    db_connection.exec("CREATE OR REPLACE FUNCTION {}()\n"
                       "RETURNS TRIGGER AS $$\n"
                       "BEGIN\n"
                       "  IF {} THEN \n"
                       "    RETURN NEW;\n"
                       "  END IF;\n"
                       "  RETURN NULL;\n"
                       "END;"
                       "$$ LANGUAGE plpgsql",
                       func_name, condition);

    db_connection.exec("CREATE TRIGGER \"{}\""
                       " BEFORE INSERT OR UPDATE"
                       " ON {}"
                       " FOR EACH ROW EXECUTE PROCEDURE"
                       " {}()",
                       table + "_osm2pgsql_valid",
                       qualified_name(schema, table), func_name);
}

void drop_geom_check_trigger(pg_conn_t const &db_connection,
                             std::string const &schema,
                             std::string const &table)
{
    std::string const func_name =
        qualified_name(schema, table + "_osm2pgsql_valid");

    db_connection.exec(R"(DROP TRIGGER "{}" ON {})", table + "_osm2pgsql_valid",
                       qualified_name(schema, table));

    db_connection.exec("DROP FUNCTION IF EXISTS {} ()", func_name);
}

void analyze_table(pg_conn_t const &db_connection, std::string const &schema,
                   std::string const &name)
{
    auto const qual_name = qualified_name(schema, name);
    db_connection.exec("ANALYZE {}", qual_name);
}

void drop_table_if_exists(pg_conn_t const &db_connection,
                          std::string const &schema, std::string const &name)
{
    db_connection.exec("DROP TABLE IF EXISTS {} CASCADE",
                       qualified_name(schema, name));
}
