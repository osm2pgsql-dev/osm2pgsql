#include "util.hpp"

namespace util {

    void exit_nicely() {
        fprintf(stderr, "Error occurred, cleaning up\n");
        exit(EXIT_FAILURE);
    }

}
