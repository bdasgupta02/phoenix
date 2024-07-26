#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

#include <chrono>
#include <string_view>

namespace stablearb {

template<typename Traits>
struct Profiler : NodeBase<Traits>
{
    using NodeBase<Traits>::NodeBase;

    struct Dummy
    {};

    template<typename Router>
    struct Timer
    {
        Timer(Router& graph, std::string_view name)
            : graph{&graph}
            , name{name}
            , start{std::chrono::high_resolution_clock::now()}
        {}

        ~Timer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            STABLEARB_LOG_INFO(*graph, name, "took", duration.count(), "µs");
        }

        Router* graph = nullptr;
        std::string_view name;
        std::chrono::high_resolution_clock::time_point start;
    };

    auto handle(auto& graph, tag::Profiler::Guard, std::string_view name)
    {
        return this->config->profiled ? Timer{graph, name} : Dummy{};
    }
};

} // namespace stablearb
