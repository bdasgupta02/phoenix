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
    inline void handle(tag::Risk::Check) { checkAbort(); }

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

} // namespace phoenix::convergence
