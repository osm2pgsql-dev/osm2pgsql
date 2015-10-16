#include "common-cleanup.hpp"
#include <boost/filesystem.hpp>

namespace cleanup {

file::file(const std::string &filename)
  : m_filename(filename) {
}

file::~file() {
  boost::system::error_code ec;
  boost::filesystem::remove(m_filename, ec);
  if (ec) {
    // not a good idea to throw an exception from a destructor,
    // so a warning will have to be good enough.
    fprintf(stderr, "WARNING: Unable to remove \"%s\": %s\n",
            m_filename.c_str(), ec.message().c_str());
  }
}

} // namespace cleanup
