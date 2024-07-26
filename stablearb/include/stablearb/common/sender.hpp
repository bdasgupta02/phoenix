#pragma once

#include "stablearb/tags.hpp"

namespace stablearb {

template<typename NodeBase>
struct Sender : NodeBase
{
    using NodeBase::NodeBase;

    void handle(tag::Sender::MassCancel) {}
};

} // namespace stablearb
