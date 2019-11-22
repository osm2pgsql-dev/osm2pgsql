#include "util.hpp"

#include <cstdlib>
#include <iostream>

namespace util {

void exit_nicely()
{
    std::cerr << "Error occurred, cleaning up\n";
    exit(EXIT_FAILURE);
}

} // namespace util
