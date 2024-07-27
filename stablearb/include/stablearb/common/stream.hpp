#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/data/fix.hpp"
#include "stablearb/tags.hpp"

#include <boost/asio.hpp>

#include <cstdint>
#include <exception>
#include <vector>

namespace {
namespace io = ::boost::asio;
} // namespace

namespace stablearb {

// Single-threaded TCP stream for Deribit with FIX
template<typename NodeBase>
struct Stream : NodeBase
{
    using PriceType = NodeBase::Traits::PriceType;

    Stream(auto const& config, auto& handler)
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
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        try
        {
            io::ip::tcp::resolver resolver{ioContext};
            auto endpoints = resolver.resolve(config->host, config->port);
            io::connect(socket, endpoints);
            STABLEARB_LOG_INFO(handler, "Connected successfully");
        }
        catch (std::exception const& e)
        {
            STABLEARB_LOG_FATAL(handler, "Connection error", e.what());
        }

        login();
        start();
    }

    void handle(tag::Stream::Stop)
    {
        // same thing as dtor
        if (!isConnected)
            return;
    }

    io::io_context ioContext;
    io::ip::tcp::socket socket{ioContext};

    bool isConnected{false};
    std::size_t nextSeqNum{1};
    std::vector<std::uint8_t> recvBuffer;
    std::vector<std::uint8_t> sendBuffer;

private:
    void login()
    {
        auto* config = this->getConfig();

        std::string_view msg = fix_msg::login(nextSeqNum, config->username, config->password, config->nonce);
        sendMsg(msg);

        // get response
    }

    inline void start()
    {
        auto* handler = this->getHandler();

        [[maybe_unused]] auto timer = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");
        // start whole pipeline
        // take last message for MD if multiple
    }

    /*inline FIXReader recvMsg()*/
    /*{*/
    /*    // read one message maybe*/
    /*    return {};*/
    /*}*/

    inline void sendMsg(std::string_view msg)
    {
        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        STABLEARB_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNum;
    }
};

} // namespace stablearb
