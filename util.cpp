#include "util.hpp"

#include <iostream>
#include <cstdlib>

namespace util {

    void exit_nicely() {
        std::cerr << "Error occurred, cleaning up\n";
        exit(EXIT_FAILURE);
    }

}
