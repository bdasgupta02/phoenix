#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/graph/router_handler.hpp"
#include "stablearb/tags.hpp"

#include <chrono>
#include <string_view>

namespace stablearb {

template<typename NodeBase>
struct Profiler : NodeBase
{
    using NodeBase::NodeBase;

    using Traits = NodeBase::Traits;
    using Router = NodeBase::Router;

    template<typename Handle>
    struct Timer
    {
        Timer()
            : enabled{false}
        {}

        Timer(Handle* handle, std::string_view name)
            : handle{handle}
            , enabled{true}
            , name{name}
            , start{std::chrono::high_resolution_clock::now()}
        {}

        ~Timer()
        {
            if (enabled)
            {
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                STABLEARB_LOG_INFO(handle, name, "took", duration.count(), "µs");
            }
        }

        Handle* handle = nullptr;
        bool enabled;
        std::string_view name;
        std::chrono::high_resolution_clock::time_point start;
    };

    Timer<RouterHandler<Router>> handle(tag::Profiler::Guard, std::string_view name)
    {
        return this->config->profiled ? Timer<RouterHandler<Router>>{this->getHandler(), name}
                                      : Timer<RouterHandler<Router>>{};
    }
};

} // namespace stablearb
