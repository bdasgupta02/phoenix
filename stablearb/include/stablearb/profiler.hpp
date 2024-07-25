#pragma once

#include "stablearb/logger.hpp"
#include "stablearb/tags.hpp"

#include <chrono>
#include <string_view>

namespace stablearb {

struct Profiler
{
    template<typename Router>
    struct Timer
    {
        Timer(Router& graph, bool enabled, std::string_view name)
            : enabled{enabled}
            , graph{&graph}
            , name{name}
            , start{std::chrono::high_resolution_clock::now()}
        {}

        ~Timer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            /*STABLEARB_LOG_INFO(*graph, name, "took", duration.count(), "µs");*/
        }

        bool enabled = false;
        Router* graph;
        std::string name;
        std::chrono::high_resolution_clock::time_point start;
    };

    auto handle(auto& graph, tag::Profiler::Guard, std::string_view name) { return Timer(graph, enabled, name); }

    bool enabled = false;
};

} // namespace stablearb
