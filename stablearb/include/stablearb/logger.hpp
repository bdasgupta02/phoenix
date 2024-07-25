#pragma once

#include <string>

namespace stablearb {

// every second, flush
struct Logger
{
    Logger()
    {
        // open file and start schedule on new thread
    }

    ~Logger()
    {
        // close file
    }

    void info(std::string msg, bool print = false) {}
};

} // namespace stablearb
