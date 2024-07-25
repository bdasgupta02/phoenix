#pragma once

#include "stablearb/data/config.hpp"
#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

#include <boost/asio.hpp>

#include <cstdint>
#include <vector>

namespace stablearb {

struct Stream : NodeBase
{
    Stream(Config const& config)
        : NodeBase{config}
        , recvBuffer(4096u)
        , sendBuffer(4096u)
    {}

    ~Stream()
    {
        // mass exit, mass cancel, logout, disconnect on dtor here
    }

    void handle(auto& graph, tag::Stream::Login)
    {
        // wrap in try catch and log
    }

    void handle(auto& graph, tag::Stream::Start)
    {
        // wrap in try catch and log
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
