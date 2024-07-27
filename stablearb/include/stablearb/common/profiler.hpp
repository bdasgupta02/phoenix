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
    using Router = NodeBase::Router;

    template<typename Handler>
    struct Timer
    {
        Timer()
            : enabled{false}
        {}

        Timer(Handler* handler, std::string_view name)
            : handler{handler}
            , enabled{true}
            , name{name}
            , start{std::chrono::high_resolution_clock::now()}
        {}

        Timer(Timer&&) = default;
        Timer& operator=(Timer&&) = default;

        Timer(Timer const&) = delete;
        Timer& operator=(Timer const&) = delete;

        ~Timer()
        {
            if (enabled) [[unlikely]]
            {
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                STABLEARB_LOG_INFO(handler, name, "took", duration.count(), "µs");
            }
        }

        Handler* handler = nullptr;
        bool enabled;
        std::string_view name;
        std::chrono::high_resolution_clock::time_point start;
    };

    inline Timer<RouterHandler<Router>> handle(tag::Profiler::Guard, std::string_view name)
    {
        return this->config->profiled ? Timer<RouterHandler<Router>>{this->getHandler(), name}
                                      : Timer<RouterHandler<Router>>{};
    }
};

} // namespace stablearb
