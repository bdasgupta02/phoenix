#pragma once

#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

namespace stablearb {

struct Sender : NodeBase
{
    using NodeBase::NodeBase;

    void handle(auto& graph, tag::Sender::MassCancel) {}
};

} // namespace stablearb
