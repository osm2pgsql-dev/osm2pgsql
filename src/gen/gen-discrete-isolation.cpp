/**
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of osm2pgsql (https://osm2pgsql.org/).
 *
 * Copyright (C) 2006-2023 by the osm2pgsql developer community.
 * For a full list of authors see the git log.
 */

#include "gen-discrete-isolation.hpp"

#include "logging.hpp"
#include "params.hpp"
#include "pgsql.hpp"
#include "util.hpp"

#include <algorithm>
#include <vector>

gen_di_t::gen_di_t(pg_conn_t *connection, params_t *params)
: gen_base_t(connection, params), m_timer_get(add_timer("get")),
  m_timer_sort(add_timer("sort")), m_timer_di(add_timer("di")),
  m_timer_reorder(add_timer("reorder")), m_timer_write(add_timer("write"))
{
    params->check_identifier_with_default("id_column", "id");
    params->check_identifier_with_default("importance_column", "importance");
}

void gen_di_t::process()
{
    struct feature
    {
        // input: unique id of the feature
        uint64_t id;

        // input: importance of the feature (positive, larger is more imporant)
        double importance;

        // input: x/y coordinate of the feature
        double x;
        double y;

        // output: discrete isolation
        double di;

        // output: rank for importance
        uint32_t irank;
    };

    log_gen("Reading data from database...");

    std::vector<feature> data;
    timer(m_timer_get).start();
    {
        auto const result = dbexec(R"(
SELECT {id_column}, {importance_column},
 ST_X({geom_column}), ST_Y({geom_column})
FROM {src} WHERE {importance_column} > 0
)");

        data.reserve(result.num_tuples());
        for (int i = 0; i < result.num_tuples(); ++i) {
            data.push_back({std::strtoull(result.get_value(i, 0), nullptr, 10),
                            std::strtod(result.get_value(i, 1), nullptr),
                            std::strtod(result.get_value(i, 2), nullptr),
                            std::strtod(result.get_value(i, 3), nullptr), 0.0,
                            0});
        }
    }
    timer(m_timer_get).stop();
    log_gen("Read {} features", data.size());

    if (data.size() < 2) {
        log_gen("Found fewer than two features. Nothing to do.");
        return;
    }

    log_gen("Sorting data by importance...");
    timer(m_timer_sort).start();
    {
        std::sort(data.begin(), data.end(),
                  [](feature const &a, feature const &b) noexcept {
                      return a.importance > b.importance;
                  });
        {
            uint32_t n = 0;
            for (auto &item : data) {
                item.irank = n++;
            }
        }
    }
    timer(m_timer_sort).stop();

    log_gen("Calculating discrete isolation...");
    timer(m_timer_di).start();
    {
        std::vector<std::pair<float, float>> coords;
        coords.reserve(data.size());
        for (auto const &d : data) {
            coords.emplace_back(d.x, d.y);
        }

        for (std::size_t n = 1; n < data.size(); ++n) {
            if (n % 10000 == 0) {
                log_gen("  {}", n);
            }
            double min = 100000000000000.0;
            for (std::size_t m = 0; m < n; ++m) {
                double const dx = coords[m].first - coords[n].first;
                double const dy = coords[m].second - coords[n].second;
                double const dist = dx * dx + dy * dy;
                if (dist < min) {
                    min = dist;
                }
            }
            data[n].di = sqrt(min);
        }
        data[0].di = data[1].di + 1;
    }
    timer(m_timer_di).stop();

    log_gen("Sorting data by discrete isolation...");
    timer(m_timer_reorder).start();
    std::sort(data.begin(), data.end(),
              [](feature const &a, feature const &b) noexcept {
                  return a.di > b.di;
              });
    timer(m_timer_reorder).stop();

    log_gen("Writing results to destination table...");
    dbexec("PREPARE update (int, real, int4, int8) AS"
           " UPDATE {src} SET dirank = $1, discr_iso = $2, irank = $3"
           " WHERE {id_column} = $4");

    timer(m_timer_write).start();
    connection().exec("BEGIN");
    std::size_t n = 0;
    for (auto const &d : data) {
        connection().exec_prepared("update", n++, d.di, d.irank, d.id);
    }
    connection().exec("COMMIT");
    timer(m_timer_write).stop();

    dbexec("ANALYZE {src}");

    log_gen("Done.");
}
