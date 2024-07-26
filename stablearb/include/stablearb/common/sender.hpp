#pragma once

#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

namespace stablearb {

template<typename Traits>
struct Sender : NodeBase<Traits>
{
    using NodeBase<Traits>::NodeBase;

    void handle(auto& graph, tag::Sender::MassCancel) {}
};

} // namespace stablearb
