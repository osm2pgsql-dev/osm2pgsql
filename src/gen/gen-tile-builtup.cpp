/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-tile-builtup.hpp"

#include "canvas.hpp"
#include "geom-functions.hpp"
#include "hex.hpp"
#include "logging.hpp"
#include "params.hpp"
#include "pgsql.hpp"
#include "raster.hpp"
#include "tile.hpp"
#include "tracer.hpp"
#include "wkb.hpp"

#include <osmium/util/string.hpp>

namespace {

std::size_t round_up(std::size_t value, std::size_t multiple) noexcept
{
    return ((value + multiple - 1U) / multiple) * multiple;
}

void save_image_to_table(pg_conn_t *connection, canvas_t const &canvas,
                         tile_t const &tile, double margin,
                         std::string const &table, char const *variant,
                         std::string const &table_prefix)
{
    auto const wkb = util::encode_hex(canvas.to_wkb(tile, margin));

    connection->exec("INSERT INTO \"{}_{}_{}\" (zoom, x, y, rast)"
                     " VALUES ({}, {}, {}, '{}')",
                     table_prefix, table, variant, tile.zoom(), tile.x(),
                     tile.y(), wkb);
}

struct param_canvas_t
{
    canvas_t canvas;
    std::string table;
};

using canvas_list_t = std::vector<param_canvas_t>;

void draw_from_db(double margin, canvas_list_t *canvas_list, pg_conn_t *conn,
                  tile_t const &tile)
{
    int prep = 0;
    auto const box = tile.box(margin);
    for (auto &cc : *canvas_list) {
        std::string const statement = "get_geoms_" + fmt::to_string(prep++);
        auto const result =
            conn->exec_prepared(statement.c_str(), box.min_x(), box.min_y(),
                                box.max_x(), box.max_y());

        for (int n = 0; n < result.num_tuples(); ++n) {
            auto const geom = ewkb_to_geom(util::decode_hex(result.get(n, 0)));
            cc.canvas.draw(geom, tile);
        }
    }
}

} // anonymous namespace

gen_tile_builtup_t::gen_tile_builtup_t(pg_conn_t *connection, bool append,
                                       params_t *params)
: gen_tile_t(connection, append, params), m_timer_draw(add_timer("draw")),
  m_timer_simplify(add_timer("simplify")),
  m_timer_vectorize(add_timer("vectorize")), m_timer_write(add_timer("write"))
{
    check_src_dest_table_params_exist();

    m_source_tables =
        osmium::split_string(get_params().get_string("src_tables"), ',');

    m_margin = get_params().get_double("margin");
    m_image_extent = uint_in_range(*params, "image_extent", 1024, 65536, 2048);
    m_image_buffer =
        uint_in_range(*params, "image_buffer", 0, m_image_extent, 0);

    auto const buffer_sizes =
        osmium::split_string(get_params().get_string("buffer_size"), ',');
    for (auto const &bs : buffer_sizes) {
        m_buffer_sizes.push_back(std::strtoul(bs.c_str(), nullptr, 10));
    }

    m_turdsize = static_cast<int>(
        uint_in_range(*params, "turdsize", 0, 65536, m_turdsize));
    m_min_area = get_params().get_double("min_area", 0.0);

    if (get_params().has("area_column")) {
        m_has_area_column = true;
        get_params().get_identifier("area_column");
    }

    if (get_params().has("img_path")) {
        m_image_path = get_params().get_string("img_path");
    }

    if (get_params().has("img_table")) {
        m_image_table = get_params().get_string("img_table");

        for (auto const &table : m_source_tables) {
            for (char const variant : {'i', 'o'}) {
                auto const table_name =
                    fmt::format("{}_{}_{}", m_image_table, table, variant);
                connection->exec(R"(
CREATE TABLE IF NOT EXISTS "{}" (
    id SERIAL PRIMARY KEY NOT NULL,
    zoom INT4,
    x INT4,
    y INT4,
    rast RASTER
)
)",
                                 table_name);
                raster_table_preprocess(table_name);
            }
        }
    }

    if (params->get_bool("make_valid")) {
        params->set("geom_sql", "(ST_Dump(ST_CollectionExtract(ST_MakeValid("
                                "$1::geometry), 3))).geom");
    } else {
        params->set("geom_sql", "$1::geometry");
    }

    if ((m_image_extent & (m_image_extent - 1)) != 0) {
        throw fmt_error(
            "The 'image_extent' parameter on generalizer{} must be power of 2.",
            context());
    }

    m_image_buffer =
        round_up(static_cast<std::size_t>(m_margin *
                                          static_cast<double>(m_image_extent)),
                 64U);
    m_margin = static_cast<double>(m_image_buffer) /
               static_cast<double>(m_image_extent);

    log_gen("Image extent: {}px, buffer: {}px, margin: {}", m_image_extent,
            m_image_buffer, m_margin);

    int n = 0;
    auto const schema = get_params().get_string("schema");
    for (auto const &src_table : m_source_tables) {
        params_t tmp_params;
        tmp_params.set("SRC", qualified_name(schema, src_table));

        dbprepare(fmt::format("get_geoms_{}", n++), tmp_params, R"(
SELECT "{geom_column}", '' AS param
 FROM {SRC}
 WHERE "{geom_column}" && ST_MakeEnvelope($1::real, $2::real, $3::real, $4::real, 3857)
)");
    }

    if (m_has_area_column) {
        dbprepare("insert_geoms", R"(
INSERT INTO {dest} ("{geom_column}", x, y, "{area_column}")
 VALUES ({geom_sql}, $2::int, $3::int, $4::real)
)");
    } else {
        dbprepare("insert_geoms", R"(
INSERT INTO {dest} ("{geom_column}", x, y)
 VALUES ({geom_sql}, $2::int, $3::int)
)");
    }
}

void gen_tile_builtup_t::process(tile_t const &tile)
{
    connection().exec("BEGIN");
    delete_existing(tile);

    canvas_list_t canvas_list;
    for (auto const &table : m_source_tables) {
        canvas_list.push_back(
            {canvas_t{m_image_extent, m_image_buffer}, table});
    }

    if (canvas_list.empty()) {
        throw std::runtime_error{"No source tables?!"};
    }

    log_gen("Read from database and draw polygons...");
    timer(m_timer_draw).start();
    draw_from_db(m_margin, &canvas_list, &connection(), tile);
    timer(m_timer_draw).stop();

    std::size_t n = 0;
    for (auto &[canvas, table] : canvas_list) {
        log_gen("Handling table='{}'", table);

        if (!m_image_path.empty()) {
            // Save input images for debugging
            save_image_to_file(canvas, tile, m_image_path, table, "i",
                               m_image_extent, m_margin);
        }

        if (!m_image_table.empty()) {
            // Store input images in database for debugging
            save_image_to_table(&connection(), canvas, tile, m_margin, table,
                                "i", m_image_table);
        }

        if (m_buffer_sizes[n] > 0) {
            log_gen("Generalize (buffer={} Mercator units)...",
                    m_buffer_sizes[n] * tile.extent() /
                        static_cast<double>(m_image_extent));
            timer(m_timer_simplify).start();
            canvas.open_close(m_buffer_sizes[n]);
            timer(m_timer_simplify).stop();
        }

        if (!m_image_path.empty()) {
            // Save output image for debugging
            save_image_to_file(canvas, tile, m_image_path, table, "o",
                               m_image_extent, m_margin);
        }

        if (!m_image_table.empty()) {
            // Store output image in database for debugging
            save_image_to_table(&connection(), canvas, tile, m_margin, table,
                                "o", m_image_table);
        }

        ++n;
    }

    log_gen("Merge bitmaps...");
    for (std::size_t n = 1; n < canvas_list.size(); ++n) {
        canvas_list[0].canvas.merge(canvas_list[n].canvas);
    }

    tracer_t tracer{m_image_extent, m_image_buffer, m_turdsize};

    log_gen("Vectorize...");
    timer(m_timer_vectorize).start();
    auto const geometries =
        tracer.trace(canvas_list[0].canvas, tile, m_min_area);
    timer(m_timer_vectorize).stop();

    log_gen("Write geometries to destination table...");
    timer(m_timer_write).start();
    for (auto const &geom : geometries) {
        auto const wkb = util::encode_hex(geom_to_ewkb(geom));
        if (m_has_area_column) {
            connection().exec_prepared("insert_geoms", wkb, tile.x(), tile.y(),
                                       geom::area(geom));
        } else {
            connection().exec_prepared("insert_geoms", wkb, tile.x(), tile.y());
        }
    }
    connection().exec("COMMIT");
    timer(m_timer_write).stop();
    log_gen("Inserted {} generalized polygons", geometries.size());
}

void gen_tile_builtup_t::post()
{
    if (!m_image_table.empty()) {
        for (auto const &table : m_source_tables) {
            for (char const variant : {'i', 'o'}) {
                raster_table_postprocess(
                    fmt::format("{}_{}_{}", m_image_table, table, variant));
            }
        }
    }

    if (!append_mode()) {
        dbexec("ANALYZE {dest}");
    }
}
