#pragma once

#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

namespace stablearb {

template<typename Traits, typename Router>
struct Sender : NodeBase<Traits, Router>
{
    using NodeBase<Traits, Router>::NodeBase;

    void handle(tag::Sender::MassCancel) {}
};

} // namespace stablearb
