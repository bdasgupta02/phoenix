#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/graph/router_handler.hpp"
#include "phoenix/tags.hpp"

#include <chrono>
#include <optional>
#include <string_view>

namespace phoenix {

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

        Timer(Timer&& other)
            : controlBlock(std::move(other.controlBlock))
        {
            other.moved = true;
        }

        Timer& operator=(Timer&& other)
        {
            this->controlBlock = std::move(other.controlBlock);
            other.moved = true;
        }

        Timer(Timer const&) = delete;
        Timer& operator=(Timer const&) = delete;

        ~Timer()
        {
            if (controlBlock && !moved) [[unlikely]]
            {
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - controlBlock->start);
                PHOENIX_LOG_INFO(
                    controlBlock->handler, "[PROFILER]", controlBlock->name, "took", duration.count(), "ns");
            }
        }

        std::optional<ControlBlock> controlBlock = std::nullopt;
        bool moved = false;
    };

    inline Timer<RouterHandler<Router>> handle(tag::Profiler::Guard, std::string_view name)
    {
        return this->config->profiled ? Timer<RouterHandler<Router>>{this->getHandler(), name}
                                      : Timer<RouterHandler<Router>>{};
    }
};

} // namespace phoenix
