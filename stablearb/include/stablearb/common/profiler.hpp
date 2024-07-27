#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/graph/router_handler.hpp"
#include "stablearb/tags.hpp"

#include <chrono>
#include <optional>
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
        struct ControlBlock
        {
            Handler* handler = nullptr;
            std::string_view name;
            std::chrono::high_resolution_clock::time_point start;
        };

        Timer() = default;
        Timer(Handler* handler, std::string_view name)
            : controlBlock{{.handler = handler, .name = name, .start = std::chrono::high_resolution_clock::now()}}
        {}

        Timer(Timer&&) = default;
        Timer& operator=(Timer&&) = default;

        Timer(Timer const&) = delete;
        Timer& operator=(Timer const&) = delete;

        ~Timer()
        {
            if (controlBlock) [[unlikely]]
            {
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - controlBlock->start);
                STABLEARB_LOG_INFO(controlBlock->handler, controlBlock->name, "took", duration.count(), "µs");
            }
        }

        std::optional<ControlBlock> controlBlock = std::nullopt;
    };

    inline Timer<RouterHandler<Router>> handle(tag::Profiler::Guard, std::string_view name)
    {
        return this->config->profiled ? Timer<RouterHandler<Router>>{this->getHandler(), name}
                                      : Timer<RouterHandler<Router>>{};
    }
};

} // namespace stablearb
