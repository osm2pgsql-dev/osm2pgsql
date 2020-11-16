
#include "logging.hpp"

/// Global logger singleton
logger the_logger{};

/// Access the global logger singleton
logger &get_logger() noexcept { return the_logger; }

