
#include "util.hpp"

namespace util {

std::string human_readable_duration(uint64_t seconds)
{
    if (seconds < 60) {
        return "{}s"_format(seconds);
    } else if (seconds < (60 * 60)) {
        return "{}s ({}m {}s)"_format(seconds, seconds / 60, seconds % 60);
    }

    auto const secs = seconds % 60;
    auto const mins = seconds / 60;
    return "{}s ({}h {}m {}s)"_format(seconds, mins / 60, mins % 60, secs);
}

} // namespace util
