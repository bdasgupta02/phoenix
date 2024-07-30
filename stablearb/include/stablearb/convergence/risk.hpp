#pragma once

#include "stablearb/data/fix.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace stablearb {

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

        if (quote.side == 1 && boundary < longPos - shortPos)
            return true;

        if (quote.side == 2 && boundary < shortPos - longPos)
            return true;

        STABLEARB_LOG_WARN(handler, "Position risk limit violated, order blocked. Position:", longPos, shortPos);
        return false;
    }

    inline void handle(tag::Risk::UpdatePosition, FIXReader&& reader)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();
        auto& instrument = config->instrument;

        auto numPositions = reader.getNumber<std::size_t>("702");

        for (std::size_t i = 0u; i < numPositions; ++i)
        {
            if (reader.getStringView("55", i) == instrument)
            {
                if (initial)
                {
                    if (reader.contains("704", i))
                        longPos = reader.getNumber<double>("704", i);

                    if (reader.contains("705", i))
                        shortPos = reader.getNumber<double>("705", i);
                }
                else
                {
                    if (reader.contains("704", i))
                        longPos += reader.getNumber<double>("704", i);

                    if (reader.contains("705", i))
                        shortPos += reader.getNumber<double>("705", i);
                }

                STABLEARB_LOG_WARN(handler, "Received position update. Current position:", longPos, shortPos);
                return;
            }
        }

        STABLEARB_LOG_WARN(handler, "No instrument position in position update");
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

} // namespace stablearb
