#pragma once

#include "stablearb/data/config.hpp"
#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

#include <boost/asio.hpp>

#include <cstdint>
#include <vector>

namespace stablearb {

template<typename Traits>
struct Stream : NodeBase<Traits>
{
    using PriceType = Traits::PriceType;

    Stream(Config<Traits> const& config)
        : NodeBase<Traits>{config}
        , recvBuffer(4096u)
        , sendBuffer(4096u)
    {}

    ~Stream()
    {
        // mass exit, mass cancel, logout, disconnect on dtor here
    }

    void handle(auto& graph, tag::Stream::Start)
    {
        // wrap in try catch and log
    }

    void handle(auto& graph, tag::Stream::Stop)
    {
        // same thing as dtor
    }

    boost::asio::io_context ioContext;
    boost::asio::ip::tcp::socket socket{ioContext};
    boost::asio::ip::tcp::resolver resolver{ioContext};

    bool isConnected{false};
    std::size_t nextSeqNum{1};
    std::vector<std::uint8_t> recvBuffer;
    std::vector<std::uint8_t> sendBuffer;
};

} // namespace stablearb
