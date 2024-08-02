#pragma once

#include "phoenix/data/fix.hpp"
#include "phoenix/tags.hpp"

#include <cstdint>

// base currency 1.0005 - 0.9995 -> don't take

namespace phoenix::triangular {

template<typename NodeBase>
struct Hitter : NodeBase
{
    using NodeBase::NodeBase;

    void handle(tag::Hitter::Hit, FIXReader&& marketData) {}

    void handle(tag::Hitter::ExecutionReport, FIXReader&& report) {}
};

} // namespace phoenix::triangular
