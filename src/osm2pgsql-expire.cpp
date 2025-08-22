/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2025 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "command-line-app.hpp"
#include "expire-config.hpp"
#include "expire-output.hpp"
#include "expire-tiles.hpp"
#include "format.hpp"
#include "geom-from-osm.hpp"
#include "geom-functions.hpp"
#include "geom.hpp"
#include "input.hpp"
#include "logging.hpp"
#include "middle-ram.hpp"
#include "middle.hpp"
#include "osmdata.hpp"
#include "output.hpp"
#include "reprojection.hpp"
#include "tile.hpp"
#include "version.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <exception>
#include <iostream>

namespace {

struct config_t
{
    expire_config_t expire_config;
    std::string input_file;
    std::string mode{"full_area"};
    std::string format{"tiles"};
    std::shared_ptr<reprojection_t> projection;
    command_t command = command_t::process;
    uint32_t zoom = 0;
};

class output_expire_t : public output_t
{
public:
    output_expire_t(std::shared_ptr<middle_query_t> const &mid,
                    std::shared_ptr<thread_pool_t> thread_pool,
                    options_t const &options, config_t const &cfg);

    output_expire_t(output_expire_t const &) = default;
    output_expire_t &operator=(output_expire_t const &) = default;

    output_expire_t(output_expire_t &&) = default;
    output_expire_t &operator=(output_expire_t &&) = default;

    ~output_expire_t() override;

    std::shared_ptr<output_t>
    clone(std::shared_ptr<middle_query_t> const &mid,
          std::shared_ptr<db_copy_thread_t> const &copy_thread) const override;

    void start() override {}
    void stop() override {}
    void sync() override {}
    void wait() override {}

    void pending_way(osmid_t /*id*/) override {}
    void pending_relation(osmid_t /*id*/) override {}

    void node_add(osmium::Node const &node) override;
    void way_add(osmium::Way *way) override;
    void relation_add(osmium::Relation const &rel) override;

    void node_modify(osmium::Node const & /*node*/) override {}
    void way_modify(osmium::Way * /*way*/) override {}
    void relation_modify(osmium::Relation const & /*rel*/) override {}

    void node_delete(osmium::Node const & /*node*/) override {}
    void way_delete(osmium::Way * /*way*/) override {}
    void relation_delete(osmium::Relation const & /*rel*/) override {}

    void merge_expire_trees(output_t * /*other*/) override {}

    void print(std::string const &format);

private:
    config_t m_config;
    expire_tiles_t m_expire_tiles;
    expire_output_t m_expire_output;
}; // class output_expire_t

std::shared_ptr<output_t> output_expire_t::clone(
    std::shared_ptr<middle_query_t> const & /*mid*/,
    std::shared_ptr<db_copy_thread_t> const & /*copy_thread*/) const
{
    return std::make_shared<output_expire_t>(*this);
}

output_expire_t::output_expire_t(std::shared_ptr<middle_query_t> const &mid,
                                 std::shared_ptr<thread_pool_t> thread_pool,
                                 options_t const &options, config_t const &cfg)
: output_t(mid, std::move(thread_pool), options), m_config(cfg),
  m_expire_tiles(cfg.zoom, cfg.projection)
{
}

output_expire_t::~output_expire_t() = default;

void output_expire_t::node_add(osmium::Node const &node)
{
    if (node.tags().empty()) {
        return;
    }

    auto const geom_merc =
        geom::transform(geom::create_point(node), *m_config.projection);

    m_expire_tiles.from_geometry(geom_merc, m_config.expire_config);
}

void output_expire_t::way_add(osmium::Way *way)
{
    if (way->tags().empty()) {
        return;
    }

    auto const counts = middle().nodes_get_list(&way->nodes());
    if (counts != way->nodes().size()) {
        log_error("Missing nodes in way {}.", way->id());
    }

    osmium::memory::Buffer buffer{1024, osmium::memory::Buffer::auto_grow::yes};

    geom::geometry_t geom;

    if (way->is_closed()) {
        log_debug("Creating polygon from closed way {}...", way->id());
        geom::create_polygon(&geom, *way, &buffer);
    }

    if (geom.is_null()) {
        log_debug("Creating linestring from way {}...", way->id());
        geom::create_linestring(&geom, *way);
    }

    if (geom.is_null()) {
        log_warn("Creating geometry from way {} failed.", way->id());
        return;
    }

    auto const geom_merc = geom::transform(geom, *m_config.projection);

    m_expire_tiles.from_geometry(geom_merc, m_config.expire_config);
}

void output_expire_t::relation_add(osmium::Relation const &relation)
{
    if (relation.tags().empty()) {
        return;
    }

    osmium::memory::Buffer buffer{1024, osmium::memory::Buffer::auto_grow::yes};

    auto const num_members = middle().rel_members_get(
        relation, &buffer,
        osmium::osm_entity_bits::node | osmium::osm_entity_bits::way);

    if (num_members == 0) {
        log_warn("No node/way members found for relation {}.", relation.id());
        return;
    }

    for (auto &node : buffer.select<osmium::Node>()) {
        if (!node.location().valid()) {
            node.set_location(middle().get_node_location(node.id()));
        }
    }

    for (auto &way : buffer.select<osmium::Way>()) {
        middle().nodes_get_list(&way.nodes());
    }

    std::string const type = relation.tags()["type"];

    osmium::memory::Buffer tmp_buffer{1024,
                                      osmium::memory::Buffer::auto_grow::yes};
    geom::geometry_t geom;
    if (type == "multipolygon") {
        log_debug("Creating multipolygon from relation {}...", relation.id());
        geom::create_multipolygon(&geom, relation, buffer, &tmp_buffer);
    } else if (type == "route" || type == "multilinestring") {
        log_debug("Creating multilinestring from relation {}...",
                  relation.id());
        geom::create_multilinestring(&geom, buffer, false);
    } else {
        log_debug("Creating geometry collection from relation {}.",
                  relation.id());
        geom::create_collection(&geom, buffer);
    }

    if (geom.is_null()) {
        log_warn("Creating geometry from relation {} failed.", relation.id());
        return;
    }

    auto const geom_merc = geom::transform(geom, *m_config.projection);

    m_expire_tiles.from_geometry(geom_merc, m_config.expire_config);
}

std::string tile_to_json(tile_t const &tile)
{
    auto const box = tile.box(0);

    nlohmann::json const feature_json = {{"type", "Feature"},
                                         {"geometry",
                                          {{"type", "Polygon"},
                                           {"coordinates",
                                            {{{box.min_x(), box.min_y()},
                                              {box.min_x(), box.max_y()},
                                              {box.max_x(), box.max_y()},
                                              {box.max_x(), box.min_y()},
                                              {box.min_x(), box.min_y()}}}}}},
                                         {"properties",
                                          {{"z", tile.zoom()},
                                           {"x", tile.x()},
                                           {"y", tile.y()},
                                           {"label", tile.to_zxy()}}}};

    return feature_json.dump();
}

std::string geojson_start()
{
    // The GeoJSON Specification (RFC 7946) only allows lon/lat coordinates,
    // but other CRSes are widely supported though this syntax from an earlier
    // draft of the GeoJSON spec.
    nlohmann::json const type_json = {
        {"type", "name"},
        {"properties", {{"name", "urn:ogc:def:crs:EPSG::3857"}}}};

    return fmt::format("{}{}{}\n", R"({"type": "FeatureCollection", "crs":)",
                       type_json.dump(), R"(, "features": [)");
}

std::string geojson_end() { return "]}\n"; }

void print_tiles(std::vector<tile_t> const &tiles)
{
    fmt::print("{}\n", geojson_start());
    bool first = true;
    for (auto const &tile : tiles) {
        fmt::print("{}{}\n", (first ? "" : ","), tile_to_json(tile));
        first = false;
    }
    fmt::print("{}", geojson_end());
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
config_t parse_command_line(int argc, char *argv[])
{
    config_t cfg;

    command_line_app_t app{"osm2pgsql-expire -- Visualize expire output\n"};
    app.init_logging_options(false, false);

    app.get_formatter()->column_width(38);

    app.add_option("OSMFILE", cfg.input_file)
        ->description("Input file")
        ->type_name("FILE");

    app.add_option("-b,--buffer", cfg.expire_config.buffer)
        ->description("Set buffer size around geometry relative to tile size")
        ->type_name("VALUE");

    app.add_option("-f,--format", cfg.format)
        ->description("Output format ('tiles', 'geojson')")
        ->type_name("FORMAT");

    app.add_option("--full-area-limit", cfg.expire_config.full_area_limit)
        ->description("Set full area limit")
        ->type_name("VALUE");

    app.add_option("-m,--mode", cfg.mode)
        ->description(
            "Set expire mode ('boundary_only', 'full_area', 'hybrid')")
        ->type_name("MODE");

    app.add_option("-z,--zoom", cfg.zoom)
        ->description("Set zoom level")
        ->type_name("ZOOM");

    try {
        app.parse(argc, argv);
    } catch (...) {
        log_info("osm2pgsql-expire version {}", get_osm2pgsql_version());
        throw;
    }

    if (app.want_help()) {
        std::cout << app.help();
        cfg.command = command_t::help;
        return cfg;
    }

    if (app.want_version()) {
        cfg.command = command_t::version;
        return cfg;
    }

    if (cfg.format != "tiles" && cfg.format != "geojson") {
        throw std::runtime_error{
            "Value for --format must be 'tiles' or 'geojson'."};
    }

    if (cfg.mode == "boundary_only") {
        cfg.expire_config.mode = expire_mode::boundary_only;
    } else if (cfg.mode == "full_area") {
        cfg.expire_config.mode = expire_mode::full_area;
    } else if (cfg.mode == "hybrid") {
        cfg.expire_config.mode = expire_mode::hybrid;
    } else {
        throw std::runtime_error{"Value for --mode must be 'boundary_only', "
                                 "'full_area', or 'hybrid'."};
    }

    return cfg;
}

void output_expire_t::print(std::string const &format)
{
    auto const tiles = m_expire_tiles.get_tiles();
    if (format == "tiles") {
        for (auto const &qk : tiles) {
            auto const tile = tile_t::from_quadkey(qk, m_config.zoom);
            fmt::print(stdout, "{}\n", tile.to_zxy());
        }
        return;
    }

    assert(format == "geojson");

    fmt::print("{}\n", geojson_start());
    bool first = true;
    for (auto const &qk : tiles) {
        fmt::print("{}{}\n", (first ? "" : ","),
                   tile_to_json(tile_t::from_quadkey(qk, m_config.zoom)));
        first = false;
    }
    fmt::print("{}", geojson_end());
}

} // anonymous namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[])
{
    try {
        auto cfg = parse_command_line(argc, argv);

        cfg.projection = reprojection_t::create_projection(PROJ_SPHERE_MERC);

        if (cfg.command == command_t::help) {
            // Already handled inside parse_command_line()
            return 0;
        }

        if (cfg.command == command_t::version) {
            print_version("osm2pgsql-expire");
            return 0;
        }

        log_info("osm2pgsql-expire version {}", get_osm2pgsql_version());
        log_warn("This is an EXPERIMENTAL extension to osm2pgsql.");

        double const distance = tile_t::EARTH_CIRCUMFERENCE /
                                static_cast<double>(1UL << cfg.zoom) *
                                cfg.expire_config.buffer;

        log_info("Settings:");
        log_info("  input_file={}", cfg.input_file);
        log_info("  buffer={}", cfg.expire_config.buffer);
        log_info("    distance={:.2f} web mercator units", distance);
        log_info("  full_area_limit={}", cfg.expire_config.full_area_limit);
        log_info("  mode={}", cfg.mode);
        log_info("  zoom={}", cfg.zoom);

        auto const input = osmium::split_string(cfg.input_file, '.');
        if (input.empty()) {
            throw std::runtime_error{"Missing input file"};
        }

        auto const &suffix = input.back();
        if (suffix == "osm" || suffix == "pbf" || suffix == "opl") {
            // input is an OSM file
            auto thread_pool = std::make_shared<thread_pool_t>(1U);
            log_debug("Started pool with {} threads.",
                      thread_pool->num_threads());

            options_t options;
            options.projection = cfg.projection;
            auto middle = create_middle(thread_pool, options);
            middle->start();

            auto output = std::make_shared<output_expire_t>(
                middle->get_query_instance(), thread_pool, options, cfg);

            osmdata_t osmdata{middle, output, options};

            std::vector<osmium::io::File> files;
            files.emplace_back(cfg.input_file);
            process_files(files, &osmdata, false, false);

            output->print(cfg.format);
        } else {
            // input is a tiles file
            std::ifstream file{cfg.input_file};
            std::string str;
            std::vector<tile_t> tiles;
            while (std::getline(file, str)) {
                tiles.push_back(tile_t::from_zxy(str));
            }
            print_tiles(tiles);
        }
    } catch (std::exception const &e) {
        log_error("{}", e.what());
        return 1;
    } catch (...) {
        log_error("Unknown exception.");
        return 1;
    }

    return 0;
}
