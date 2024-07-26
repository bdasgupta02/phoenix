#pragma once

#include "stablearb/graph/node_base.hpp"

namespace stablearb {

template<typename Traits>
struct Quoter : NodeBase<Traits>
{
    using NodeBase<Traits>::NodeBase;

    using PriceType = Traits::PriceType;
};

} // namespace stablearb
