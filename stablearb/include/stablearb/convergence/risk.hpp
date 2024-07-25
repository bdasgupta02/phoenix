#pragma once

#include "stablearb/graph/node_base.hpp"

namespace stablearb {

template<typename Traits>
struct Risk : NodeBase
{
    using NodeBase::NodeBase;

    using PriceType = Traits::PriceType;
};

} // namespace stablearb
