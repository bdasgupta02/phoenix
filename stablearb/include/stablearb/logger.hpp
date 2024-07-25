#pragma once

#include "stablearb/tags.hpp"

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

    void handle(auto&, tag::Logger::Info, std::string msg, bool print = false, std::string file = __FILE__) {}
};

} // namespace stablearb
