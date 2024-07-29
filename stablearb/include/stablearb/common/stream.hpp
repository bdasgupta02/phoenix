#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/data/fix.hpp"
#include "stablearb/tags.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

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
    {
        recvBuffer.prepare(8192u);
    }

    void handle(tag::Stream::Stop)
    {
        if (!isRunning)
            return;

        auto* handler = this->getHandler();
        STABLEARB_LOG_INFO(handler, "Stopping stream");
        isRunning = false;
        sendMsg(fixBuilder.logout(nextSeqNum));

        boost::system::error_code ec;
        socket.close(ec);
        if (ec)
            STABLEARB_LOG_ERROR(handler, "Error closing socket", ec.message());
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
            isRunning = true;
        }
        catch (std::exception const& e)
        {
            STABLEARB_LOG_ERROR(handler, "Connection error", e.what());
            return;
        }

        login();
        startPipeline();
    }

private:
    inline void startPipeline()
    {
        auto* handler = this->getHandler();
        STABLEARB_LOG_INFO(handler, "Starting trading pipeline");

        while (isRunning)
        {
            [[maybe_unused]] auto timer = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");
            try
            {}
            catch (std::exception const& e)
            {
                STABLEARB_LOG_FATAL(handler, "Error in trading pipeline", e.what());
                break;
            }
        }
    }

    inline void login()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        std::string_view msg = fixBuilder.login(nextSeqNum, config->username, config->secret);
        sendMsg(msg);
        auto reader = recvMsg();

        STABLEARB_LOG_VERIFY(handler, (reader.getString("35") == "A"), "Login unsuccessful");
        STABLEARB_LOG_INFO(handler, "Login successful");
    }

    inline FIXReader recvMsg()
    {
        io::read_until(socket, recvBuffer, boost::regex("10=\\d+\\x01"));
        char const* data = boost::asio::buffer_cast<char const*>(recvBuffer.data());
        std::size_t size = recvBuffer.size();
        std::string_view str{data, size};

        FIXReader reader{str};
        STABLEARB_LOG_VERIFY(this->getHandler(), (reader.getString("35") != "3"), "Reject message received", str);
        recvBuffer.consume(size);
        return std::move(reader);
    }

    inline void sendMsg(std::string_view msg)
    {
        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        STABLEARB_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNum;
    }

    io::io_context ioContext;
    io::ip::tcp::socket socket{ioContext};
    io::streambuf recvBuffer;

    bool isRunning = false;
    std::size_t nextSeqNum = 1u;

    FIXMessageBuilder fixBuilder;
};

} // namespace stablearb
