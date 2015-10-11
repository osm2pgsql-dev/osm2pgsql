#ifndef COMMON_CLEANUP_HPP
#define COMMON_CLEANUP_HPP

#include <string>

namespace cleanup {

// RAII structure to remove a file upon destruction. doesn't create or do
// anything to the file when it constructs.
struct file {
  file(const std::string &filename);
  ~file();
private:
  // name of the file to be deleted upon destruction
  std::string m_filename;
};


} // namespace cleanup

#endif /* COMMON_CLEANUP_HPP */
