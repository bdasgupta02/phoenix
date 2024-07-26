#pragma once

#include "stablearb/data/config.hpp"
#include "stablearb/tags.hpp"

#include <boost/asio.hpp>

#include <cstdint>
#include <vector>

namespace stablearb {

template<typename NodeBase>
struct Stream : NodeBase
{
    using PriceType = NodeBase::Traits::PriceType;

    template<typename Traits, typename Router>
    Stream(Config<Traits> const& config, RouterHandler<Router>& handler)
        : NodeBase{config, handler}
        , recvBuffer(4096u)
        , sendBuffer(4096u)
    {}

    ~Stream()
    {
        // mass exit, mass cancel, logout, disconnect on dtor here
    }

    void handle(tag::Stream::Start)
    {
        // wrap in try catch and log
    }

    void handle(tag::Stream::Stop)
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
