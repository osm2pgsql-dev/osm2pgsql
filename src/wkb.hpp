#ifndef OSM2PGSQL_WKB_HPP
#define OSM2PGSQL_WKB_HPP

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <osmium/geom/coordinates.hpp>
#include <osmium/geom/factory.hpp>

namespace ewkb {

enum geometry_type : uint32_t
{
    wkb_point = 1,
    wkb_line = 2,
    wkb_polygon = 3,
    wkb_multi_point = 4,
    wkb_multi_line = 5,
    wkb_multi_polygon = 6,
    wkb_collection = 7,

    wkb_srid = 0x20000000 // SRID-presence flag (EWKB)
};

enum wkb_byte_order_type_t : uint8_t
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    Endian = 1 // Little Endian
#else
    Endian = 0, // Big Endian
#endif
};

/**
 *  Writer for EWKB data suitable for postgres.
 *
 *  Code has been largely derived from osmium::geom::WKBFactoryImpl.
 */
class writer_t
{

    std::string m_data;
    int m_srid;

    size_t m_geometry_size_offset = 0;
    size_t m_multigeometry_size_offset = 0;
    size_t m_ring_size_offset = 0;

    size_t header(std::string &str, geometry_type type, bool add_length) const
    {
        str_push(str, Endian);
        str_push(str, type | wkb_srid);
        str_push(str, m_srid);

        std::size_t const offset = str.size();
        if (add_length) {
            str_push(str, static_cast<uint32_t>(0));
        }
        return offset;
    }

    void set_size(size_t const offset, size_t const size)
    {
        uint32_t s = static_cast<uint32_t>(size);
        std::copy_n(reinterpret_cast<char *>(&s), sizeof(uint32_t),
                    &m_data[offset]);
    }

    template <typename T>
    inline static void str_push(std::string &str, T data)
    {
        str.append(reinterpret_cast<char const *>(&data), sizeof(T));
    }

public:
    explicit writer_t(int srid) : m_srid(srid) {}

    void add_sub_geometry(std::string const &part) { m_data.append(part); }

    void add_location(osmium::geom::Coordinates const &xy)
    {
        str_push(m_data, xy.x);
        str_push(m_data, xy.y);
    }

    /* Point */

    std::string make_point(osmium::geom::Coordinates const &xy) const
    {
        std::string data;
        header(data, wkb_point, false);
        str_push(data, xy.x);
        str_push(data, xy.y);

        return data;
    }

    /* LineString */

    void linestring_start()
    {
        m_geometry_size_offset = header(m_data, wkb_line, true);
    }

    std::string linestring_finish(size_t num_points)
    {
        set_size(m_geometry_size_offset, num_points);
        std::string data;

        using std::swap;
        swap(data, m_data);

        return data;
    }

    /* MultiLineString */

    void multilinestring_start()
    {
        m_multigeometry_size_offset = header(m_data, wkb_multi_line, true);
    }

    std::string multilinestring_finish(size_t num_lines)
    {
        set_size(m_multigeometry_size_offset, num_lines);
        std::string data;

        using std::swap;
        swap(data, m_data);

        return data;
    }

    /* Polygon */

    void polygon_start()
    {
        m_geometry_size_offset = header(m_data, wkb_polygon, true);
    }

    void polygon_ring_start()
    {
        m_ring_size_offset = m_data.size();
        str_push(m_data, static_cast<uint32_t>(0));
    }

    void polygon_ring_finish(size_t num_points)
    {
        set_size(m_ring_size_offset, num_points);
    }

    std::string polygon_finish(size_t num_rings)
    {
        set_size(m_geometry_size_offset, num_rings);
        std::string data;

        using std::swap;
        swap(data, m_data);

        return data;
    }

    /* MultiPolygon */

    void multipolygon_start()
    {
        m_multigeometry_size_offset = header(m_data, wkb_multi_polygon, true);
    }

    std::string multipolygon_finish(size_t num_polygons)
    {
        set_size(m_multigeometry_size_offset, num_polygons);
        std::string data;

        using std::swap;
        swap(data, m_data);

        return data;
    }
};

/**
 * Class that allows to iterate over the elements of a ewkb geometry.
 *
 * Note: this class assumes that the wkb was created by ewkb::writer_t.
 *       It implements the exact opposite decoding.
 */
class parser_t
{
public:
    inline static std::string wkb_from_hex(std::string const &wkb)
    {
        std::string out;

        bool front = true;
        char outc;
        for (char c : wkb) {
            c -= 48;
            if (c > 9) {
                c -= 7;
            }
            if (front) {
                outc = char(c << 4);
                front = false;
            } else {
                out += outc | c;
                front = true;
            }
        }

        if (out[0] != Endian) {
            throw std::runtime_error
            {
#if __BYTE_ORDER == __LITTLE_ENDIAN
                "Geometries in the database are returned in big-endian byte "
                "order. "
#else
                "Geometries in the database are returned in little-endian byte "
                "order. "
#endif
                "osm2pgsql can only process geometries in native byte order."
            };
        }
        return out;
    }

    explicit parser_t(char const *wkb) : m_wkb(wkb), m_pos(0) {}
    explicit parser_t(std::string const &wkb) : m_wkb(wkb.c_str()), m_pos(0) {}

    size_t save_pos() const { return m_pos; }
    void rewind(size_t pos) { m_pos = pos; }

    int read_header()
    {
        m_pos += sizeof(uint8_t); // skip endianess

        auto const type = read_data<uint32_t>();

        if (type & wkb_srid) {
            m_pos += sizeof(int); // skip srid
        }

        return type & 0xffU;
    }

    uint32_t read_length() { return read_data<uint32_t>(); }

    osmium::geom::Coordinates read_point()
    {
        auto const x = read_data<double>();
        auto const y = read_data<double>();

        return osmium::geom::Coordinates{x, y};
    }

    void skip_points(size_t num) { m_pos += sizeof(double) * 2 * num; }

    template <typename PROJ>
    double get_area(PROJ *proj = nullptr)
    {
        double total = 0;

        auto const type = read_header();

        if (type == wkb_polygon) {
            total = get_polygon_area(proj);
        } else if (type == wkb_multi_polygon) {
            auto const num_poly = read_length();
            for (unsigned i = 0; i < num_poly; ++i) {
                auto ptype = read_header();
                (void)ptype;
                assert(ptype == wkb_polygon);

                total += get_polygon_area(proj);
            }
        }

        return total;
    }

private:
    template <typename PROJ>
    double get_polygon_area(PROJ *proj)
    {
        auto const num_rings = read_length();
        assert(num_rings > 0);

        double total = get_ring_area(proj);

        for (unsigned i = 1; i < num_rings; ++i) {
            total -= get_ring_area(proj);
        }

        return total;
    }

    template <typename PROJ>
    double get_ring_area(PROJ *proj)
    {
        // Algorithm borrowed from
        // http://stackoverflow.com/questions/451426/how-do-i-calculate-the-area-of-a-2d-polygon
        // XXX numerically not stable (useless for latlon)
        auto const num_pts = read_length();
        assert(num_pts > 3);

        double total = 0;

        auto prev = proj->target_to_tile(read_point());
        for (unsigned i = 1; i < num_pts; ++i) {
            auto const cur = proj->target_to_tile(read_point());
            total += prev.x * cur.y - cur.x * prev.y;
            prev = cur;
        }

        return std::abs(total) * 0.5;
    }

    double get_ring_area(osmium::geom::IdentityProjection *)
    {
        // Algorithm borrowed from
        // http://stackoverflow.com/questions/451426/how-do-i-calculate-the-area-of-a-2d-polygon
        auto const num_pts = read_length();
        assert(num_pts > 3);

        double total = 0;

        auto prev = read_point();
        for (unsigned i = 1; i < num_pts; ++i) {
            auto const cur = read_point();
            total += prev.x * cur.y - cur.x * prev.y;
            prev = cur;
        }

        return std::abs(total) * 0.5;
    }

    template <typename T>
    T read_data()
    {
        T data;
        memcpy(&data, m_wkb + m_pos, sizeof(T));
        m_pos += sizeof(T);

        return data;
    }

    char const *m_wkb;
    size_t m_pos;
};

} // namespace ewkb

#endif // OSM2PGSQL_WKB_HPP
