#ifndef OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP
#define OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP

#include "format.hpp"

#include <string>

#include <boost/filesystem.hpp>

namespace testing {
namespace cleanup {

/**
 * RAII structure to remove a file upon destruction.
 *
 * Per default will also make sure that the file does not exist
 * when it is constructed.
 */
class file_t
{
public:
    file_t(std::string const &filename, bool remove_on_construct = true)
    : m_filename(filename)
    {
        if (remove_on_construct) {
            delete_file(false);
        }
    }

    ~file_t() noexcept { delete_file(true); }

private:
    void delete_file(bool warn) const noexcept
    {
        if (m_filename.empty()) {
            return;
        }

        boost::system::error_code ec;
        boost::filesystem::remove(m_filename, ec);
        if (ec && warn) {
            fmt::print(stderr, "WARNING: Unable to remove \"{}\": {}\n",
                       m_filename, ec.message());
        }
    }

    std::string m_filename;
};

} // namespace cleanup
} // namespace testing

#endif // OSM2PGSQL_TESTS_COMMON_CLEANUP_HPP
