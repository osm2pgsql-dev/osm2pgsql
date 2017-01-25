
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <string>

#include <osmium/geom/coordinates.hpp>
#include <osmium/geom/factory.hpp>

/**
 * Class that allows to iterate over the elements of a ewkb geometry.
 *
 * Note: this class assumes that the wkb was created by osmium::geom::WKBFactory.
 *       It implements the exact opposite decoding.
 */
class ewkb_parser_t
{
public:
    enum geometry_type
    {
        wkb_point = 1,
        wkb_line = 2,
        wkb_polygon = 3,
        wkb_multi_point = 4,
        wkb_multi_line = 5,
        wkb_multi_polygon = 6,
        wkb_collection = 7,
    };

    explicit ewkb_parser_t(char const *wkb) : m_wkb(wkb), m_pos(0) {}
    explicit ewkb_parser_t(std::string const &wkb)
    : m_wkb(wkb.c_str()), m_pos(0)
    {
    }

    unsigned save_pos() const { return m_pos; }
    void rewind(unsigned pos) { m_pos = pos; }

    int read_header()
    {
        m_pos += sizeof(uint8_t); // skip endianess

        auto type = read_data<uint32_t>();

        m_pos += sizeof(int); // skip srid

        return type & 0xff;
    }

    uint32_t read_length() { return read_data<uint32_t>(); }

    osmium::geom::Coordinates read_point()
    {
        auto x = read_data<double>();
        auto y = read_data<double>();

        return osmium::geom::Coordinates(x, y);
    }

    void skip_points(size_t num) { m_pos += sizeof(double) * 2 * num; }

    template <typename PROJ>
    double get_area(PROJ *proj = nullptr)
    {
        double total = 0;

        auto type = read_header();

        if (type == wkb_polygon) {
            total = get_polygon_area(proj);
        } else if (type == wkb_multi_polygon) {
            auto num_poly = read_length();
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
        auto num_rings = read_length();
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
        auto num_pts = read_length();
        assert(num_pts > 3);

        double total = 0;

        auto prev = read_point();
        proj->target_to_tile(&prev.y, &prev.x);
        for (unsigned i = 1; i < num_pts; ++i) {
            auto cur = read_point();
            proj->target_to_tile(&cur.y, &cur.x);
            total += prev.x * cur.y - cur.x * prev.y;
            prev = cur;
        }

        return std::abs(total) * 0.5;
    }

    double get_ring_area(osmium::geom::IdentityProjection *)
    {
        // Algorithm borrowed from
        // http://stackoverflow.com/questions/451426/how-do-i-calculate-the-area-of-a-2d-polygon
        auto num_pts = read_length();
        assert(num_pts > 3);

        double total = 0;

        auto prev = read_point();
        for (unsigned i = 1; i < num_pts; ++i) {
            auto cur = read_point();
            total += prev.x * cur.y - cur.x * prev.y;
            prev = cur;
        }

        return std::abs(total) * 0.5;
    }

    template <typename T>
    T read_data()
    {
        auto *data = reinterpret_cast<T const *>(m_wkb + m_pos);
        m_pos += sizeof(T);

        return *data;
    }

    char const *m_wkb;
    unsigned m_pos;
};
