#pragma once

#include "stablearb/graph/node_base.hpp"

namespace stablearb {

template<typename Traits, typename Router>
struct Quoter : NodeBase<Traits, Router>
{
    using NodeBase<Traits, Router>::NodeBase;

    using PriceType = Traits::PriceType;
};

} // namespace stablearb
