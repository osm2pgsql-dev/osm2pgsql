/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-tile-raster.hpp"

#include "canvas.hpp"
#include "logging.hpp"
#include "params.hpp"
#include "pgsql.hpp"
#include "raster.hpp"
#include "tile.hpp"
#include "tracer.hpp"
#include "wkb.hpp"

#include <unordered_map>

static std::size_t round_up(std::size_t value, std::size_t multiple) noexcept
{
    return ((value + multiple - 1U) / multiple) * multiple;
}

gen_tile_raster_union_t::gen_tile_raster_union_t(pg_conn_t *connection,
                                                 params_t *params)
: gen_tile_t(connection, params), m_timer_draw(add_timer("draw")),
  m_timer_simplify(add_timer("simplify")),
  m_timer_vectorize(add_timer("vectorize")), m_timer_write(add_timer("write"))
{
    m_margin = get_params().get_double("margin");
    m_image_extent = uint_in_range(*params, "image_extent", 1024, 65536, 2048);
    m_image_buffer =
        uint_in_range(*params, "image_buffer", 0, m_image_extent, 0);
    m_buffer_size = uint_in_range(*params, "buffer_size", 1, 65536, 10);
    m_turdsize = static_cast<int>(
        uint_in_range(*params, "turdsize", 0, 65536, m_turdsize));

    if (get_params().has("img_path")) {
        m_image_path = get_params().get_string("img_path");
    }

    if (get_params().has("img_table")) {
        m_image_table = get_params().get_string("img_table");

        for (char const variant : {'i', 'o'}) {
            auto const table_name =
                fmt::format("{}_{}", m_image_table, variant);
            connection->exec(R"(
CREATE TABLE IF NOT EXISTS "{}" (
    type TEXT,
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

    if (get_params().get_bool("make_valid")) {
        params->set(
            "geom_sql",
            "(ST_Dump(ST_CollectionExtract(ST_MakeValid($1), 3))).geom");
    } else {
        params->set("geom_sql", "$1");
    }

    if (m_image_extent < 1024U) {
        throw std::runtime_error{"width must be at least 1024"};
    }

    if ((m_image_extent & (m_image_extent - 1)) != 0) {
        throw std::runtime_error{"width must be power of 2"};
    }

    m_image_buffer =
        round_up(static_cast<std::size_t>(m_margin *
                                          static_cast<double>(m_image_extent)),
                 64U);
    m_margin = static_cast<double>(m_image_buffer) /
               static_cast<double>(m_image_extent);

    log_gen("Image extent: {}px, buffer: {}px, margin: {}", m_image_extent,
            m_image_buffer, m_margin);

    if (with_group_by()) {
        dbexec(R"(
PREPARE get_geoms (real, real, real, real) AS
 SELECT "{geom_column}", "{group_by_column}"
 FROM {src}
 WHERE "{geom_column}" && ST_MakeEnvelope($1, $2, $3, $4, 3857)
)");
        dbexec(R"(
PREPARE insert_geoms (geometry, int, int, text) AS
 INSERT INTO {dest} ("{geom_column}", x, y, "{group_by_column}")
 VALUES ({geom_sql}, $2, $3, $4)
)");
    } else {
        dbexec(R"(
PREPARE get_geoms (real, real, real, real) AS
 SELECT "{geom_column}", NULL AS param
 FROM {src}
 WHERE "{geom_column}" && ST_MakeEnvelope($1, $2, $3, $4, 3857)
)");
        dbexec(R"(
PREPARE insert_geoms (geometry, int, int, text) AS
 INSERT INTO {dest} ("{geom_column}", x, y) VALUES ({geom_sql}, $2, $3)
)");
    }
}

static void save_image_to_table(pg_conn_t *connection, canvas_t const &canvas,
                                tile_t const &tile, double margin,
                                std::string const &param, char const *variant,
                                std::string const &table_prefix)
{
    auto const wkb = to_hex(canvas.to_wkb(tile, margin));

    connection->exec("INSERT INTO \"{}_{}\" (type, zoom, x, y, rast)"
                     " VALUES ('{}', {}, {}, {}, '{}')",
                     table_prefix, variant, param, tile.zoom(), tile.x(),
                     tile.y(), wkb);
}

namespace {

struct param_canvas_t
{
    canvas_t canvas;
    std::size_t points = 0;

    param_canvas_t(unsigned int image_extent, unsigned int image_buffer)
    : canvas(image_extent, image_buffer)
    {}
};

} // anonymous namespace

using canvas_list_t = std::unordered_map<std::string, param_canvas_t>;

static void draw_from_db(double margin, unsigned int image_extent,
                         unsigned int image_buffer, canvas_list_t *canvas_list,
                         pg_conn_t *conn, tile_t const &tile)
{
    auto const box = tile.box(margin);
    auto const result = conn->exec_prepared(
        "get_geoms", box.min_x(), box.min_y(), box.max_x(), box.max_y());

    for (int n = 0; n < result.num_tuples(); ++n) {
        std::string param = result.get_value(n, 1);
        auto const geom = ewkb_to_geom(decode_hex(result.get_value(n, 0)));

        auto const [it, success] = canvas_list->try_emplace(
            std::move(param), image_extent, image_buffer);

        it->second.points += it->second.canvas.draw(geom, tile);
    }
}

void gen_tile_raster_union_t::process(tile_t const &tile)
{
    delete_existing(tile);

    canvas_list_t canvas_list;

    log_gen("Read from database and draw polygons...");
    timer(m_timer_draw).start();
    draw_from_db(m_margin, m_image_extent, m_image_buffer, &canvas_list,
                 &connection(), tile);
    timer(m_timer_draw).stop();

    for (auto &cp : canvas_list) {
        auto const &param = cp.first;
        auto &[canvas, points] = cp.second;
        log_gen("Handling param='{}'", param);

        if (!m_image_path.empty()) {
            // Save input image for debugging
            save_image_to_file(canvas, tile, m_image_path, param, "i",
                               m_image_extent, m_margin);
        }

        if (!m_image_table.empty()) {
            // Store input image in database for debugging
            save_image_to_table(&connection(), canvas, tile, m_margin, param,
                                "i", m_image_table);
        }

        if (m_buffer_size > 0) {
            log_gen("Generalize (buffer={} Mercator units)...",
                    m_buffer_size * tile.extent() /
                        static_cast<double>(m_image_extent));
            timer(m_timer_simplify).start();
            canvas.open_close(m_buffer_size);
            timer(m_timer_simplify).stop();
        }

        if (!m_image_path.empty()) {
            // Save output image for debugging
            save_image_to_file(canvas, tile, m_image_path, param, "o",
                               m_image_extent, m_margin);
        }

        if (!m_image_table.empty()) {
            // Store output image in database for debugging
            save_image_to_table(&connection(), canvas, tile, m_margin, param,
                                "o", m_image_table);
        }

        tracer_t tracer{m_image_extent, m_image_buffer, m_turdsize};

        log_gen("Vectorize...");
        timer(m_timer_vectorize).start();
        auto const geometries = tracer.trace(canvas, tile);
        timer(m_timer_vectorize).stop();

        log_gen("Reduced from {} points to {} points ({:.1f} %)", points,
                tracer.num_points(),
                static_cast<double>(tracer.num_points()) /
                    static_cast<double>(points) * 100);

        log_gen("Write geometries to destination table...");
        timer(m_timer_write).start();
        for (auto const &geom : geometries) {
            auto const wkb = geom_to_ewkb(geom);
            connection().exec_prepared("insert_geoms", binary_param{wkb},
                                       tile.x(), tile.y(), param);
        }
        timer(m_timer_write).stop();
        log_gen("Inserted {} generalized polygons", geometries.size());
    }
}

void gen_tile_raster_union_t::post()
{
    if (!m_image_table.empty()) {
        for (char const variant : {'i', 'o'}) {
            raster_table_postprocess(
                fmt::format("{}_{}", m_image_table, variant));
        }
    }
    dbexec("ANALYZE {dest}");
}
