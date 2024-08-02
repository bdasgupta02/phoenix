#pragma once

#include "phoenix/data/fix.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace phoenix::convergence {

template<typename NodeBase>
struct Risk : NodeBase
{
    using NodeBase::NodeBase;

    using Traits = NodeBase::Traits;
    using PriceType = NodeBase::Traits::PriceType;

    void handle(tag::Risk::Abort) { aborted.test_and_set(); }

    inline bool handle(tag::Risk::Check, auto& quote)
    {
        checkAbort();

        auto* handler = this->getHandler();
        auto* config = this->getConfig();
        auto boundary = config->positionBoundary;

        if (quote.side == 1 && boundary >= longPos - shortPos)
            return true;

        if (quote.side == 2 && boundary >= shortPos - longPos)
            return true;

        PHOENIX_LOG_WARN(
            handler,
            "Position risk limit violated, order blocked. Position:",
            longPos,
            shortPos,
            "with boundary",
            boundary);
        return false;
    }

    inline void handle(tag::Risk::UpdatePosition, double pos, unsigned int side)
    {
        if (side == 1)
            longPos += pos;

        if (side == 2)
            shortPos += pos;

        PHOENIX_LOG_INFO(this->getHandler(), "Position updated with long", longPos, "and short", shortPos);
    }

private:
    inline void checkAbort()
    {
        if (aborted.test())
        {
            // cancel on disconnect is enabled on login
            this->getHandler()->invoke(tag::Stream::Stop{});
            this->getHandler()->invoke(tag::Logger::Stop{});
            std::abort();
        }
    }

    bool initial = true;

    double longPos = 0.0;
    double shortPos = 0.0;

    std::atomic_flag aborted = ATOMIC_FLAG_INIT;
};

} // namespace phoenix
