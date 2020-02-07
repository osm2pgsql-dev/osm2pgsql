#ifndef OSM2PGSQL_TESTS_COMMON_OPTIONS_HPP
#define OSM2PGSQL_TESTS_COMMON_OPTIONS_HPP

#include "options.hpp"

#include "common-pg.hpp"

namespace testing {

class opt_t
{
public:
    opt_t()
    {
        m_opt.prefix = "osm2pgsql_test";
        m_opt.style = OSM2PGSQLDATA_DIR "default.style";
        m_opt.num_procs = 1;
        m_opt.cache = 2;
        m_opt.append = false;
        m_opt.projection = reprojection::create_projection(PROJ_SPHERE_MERC);
    }

    operator options_t() const { return m_opt; }

    opt_t &slim()
    {
        m_opt.slim = true;
        return *this;
    }

    opt_t &slim(pg::tempdb_t const &db)
    {
        m_opt.slim = true;
        m_opt.database_options = db.db_options();
        return *this;
    }

    opt_t &append()
    {
        m_opt.append = true;
        return *this;
    }

    opt_t &gazetteer()
    {
        m_opt.output_backend = "gazetteer";
        m_opt.style = TESTDATA_DIR "gazetteer-test.style";
        return *this;
    }

    opt_t &multi(char const *style)
    {
        m_opt.output_backend = "multi";
        m_opt.style = TESTDATA_DIR;
        m_opt.style += style;
        return *this;
    }

    opt_t &flex(char const *style)
    {
        m_opt.output_backend = "flex";
        m_opt.style = TESTDATA_DIR;
        m_opt.style += style;
        return *this;
    }

    opt_t &flatnodes()
    {
        m_opt.flat_node_file =
            boost::optional<std::string>("test_middle_flat.flat.nodes.bin");
        m_opt.flat_node_cache_enabled = true;
        return *this;
    }

    opt_t &style(char const *filename)
    {
        m_opt.style = TESTDATA_DIR;
        m_opt.style += filename;
        return *this;
    }

    opt_t &srs(int srs)
    {
        m_opt.projection = reprojection::create_projection(srs);
        return *this;
    }

    opt_t &extra_attributes() noexcept
    {
        m_opt.extra_attributes = true;
        return *this;
    }

private:
    options_t m_opt;
};

} // namespace testing

#endif // OSM2PGSQL_TESTS_COMMON_OPTIONS_HPP
