/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "debug-output.hpp"

void write_tileset_list_to_debug_log(
    std::vector<flex_tileset_t> const &tilesets)
{
    if (!get_logger().debug_enabled()) {
        return;
    }

    log_debug("Tilesets:");
    for (auto const &tileset : tilesets) {
        log_debug("- TILESET {}", tileset.name());
        if (tileset.minzoom() == tileset.maxzoom()) {
            log_debug("  - zoom: {}", tileset.maxzoom());
        } else {
            log_debug("  - zoom: {}-{}", tileset.minzoom(), tileset.maxzoom());
        }
        if (!tileset.filename().empty()) {
            log_debug("  - filename: {}", tileset.filename());
        }
        if (!tileset.table().empty()) {
            log_debug("  - table: {}",
                      qualified_name(tileset.schema(), tileset.name()));
        }
    }
}

void write_table_list_to_debug_log(std::vector<flex_table_t> const &tables,
                                   std::vector<flex_tileset_t> const &tilesets)
{
    if (!get_logger().debug_enabled()) {
        return;
    }

    log_debug("Tables:");
    for (auto const &table : tables) {
        log_debug("- TABLE {}", qualified_name(table.schema(), table.name()));
        log_debug("  - columns:");
        for (auto const &column : table) {
            log_debug(R"(    - "{}" {} ({}) not_null={} create_only={})",
                      column.name(), column.type_name(), column.sql_type_name(),
                      column.not_null(), column.create_only());
            for (auto const &ec : column.expire_configs()) {
                auto const &config = (tilesets)[ec.tileset];
                log_debug("      - expire: tileset={}", config.name());
            }
        }
        log_debug("  - data_tablespace={}", table.data_tablespace());
        log_debug("  - index_tablespace={}", table.index_tablespace());
        log_debug("  - cluster={}", table.cluster_by_geom());
        for (auto const &index : table.indexes()) {
            log_debug("  - INDEX USING {}", index.method());
            log_debug("    - column={}", index.columns());
            log_debug("    - expression={}", index.expression());
            log_debug("    - include={}", index.include_columns());
            log_debug("    - tablespace={}", index.tablespace());
            log_debug("    - unique={}", index.is_unique());
            log_debug("    - where={}", index.where_condition());
        }
    }
}
