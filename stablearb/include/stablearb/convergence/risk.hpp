#pragma once

#include "stablearb/graph/node_base.hpp"

#include <atomic>

namespace stablearb {

template<typename Traits>
struct Risk : NodeBase
{
    using NodeBase::NodeBase;

    using PriceType = Traits::PriceType;

    void handle(auto&, tag::Risk::Abort) { aborted.test_and_set(); }

private:
    void abort(auto& graph)
    {
        graph.invoke(tag::Sender::MassCancel{});
        graph.invoke(tag::Stream::Stop{});
        graph.invoke(tag::Logger::Stop{});
        std::abort();
    }

    // To be checked everytime a handler except Abort is called
    std::atomic_flag aborted = ATOMIC_FLAG_INIT;
};

} // namespace stablearb
