#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

#include <chrono>
#include <string_view>

namespace stablearb {

template<typename Traits, typename Router>
struct Profiler : NodeBase<Traits, Router>
{
    using NodeBase<Traits, Router>::NodeBase;

    struct Dummy
    {};

    template<typename Handle>
    struct Timer
    {
        Timer(Handle* handle, std::string_view name)
            : handle{handle}
            , name{name}
            , start{std::chrono::high_resolution_clock::now()}
        {}

        ~Timer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            STABLEARB_LOG_INFO(handle, name, "took", duration.count(), "µs");
        }

        Handle* handle = nullptr;
        std::string_view name;
        std::chrono::high_resolution_clock::time_point start;
    };

    auto handle(tag::Profiler::Guard, std::string_view name)
    {
        return this->config->profiled ? Timer{*(this->handler), name} : Dummy{};
    }
};

} // namespace stablearb
