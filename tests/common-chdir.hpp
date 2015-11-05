#ifndef COMMON_CHDIR_HPP
#define COMMON_CHDIR_HPP

#include <cstdlib>
#include <boost/filesystem.hpp>

static inline void auto_chdir() {
  char* testDir = getenv ("TEST_DATA_DIR");
  if (testDir!=nullptr) {
      boost::filesystem::current_path(testDir);
      fprintf(stderr, "Changed directory to %s\n", boost::filesystem::current_path().string().c_str());
  }
}
#endif /* COMMON_CHDIR_HPP */
