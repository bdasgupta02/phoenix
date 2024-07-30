#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/data/fix.hpp"
#include "stablearb/data/quotes.hpp"
#include "stablearb/tags.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <vector>

#include <immintrin.h>

namespace {
namespace io = ::boost::asio;
} // namespace

namespace stablearb {

// Single-threaded TCP stream for Deribit with FIX, specifically for convergence where liquidity is low
template<typename NodeBase>
struct Stream : NodeBase
{
    using Traits = NodeBase::Traits;
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

    inline void handle(tag::Stream::SendQuote, SingleQuote<Traits>& quote)
    {
        auto msg = fixBuilder.newOrderSingle(nextSeqNum, this->getConfig()->instrument, quote);
        sendMsg(msg);
    }

private:
    inline void startPipeline()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        STABLEARB_LOG_INFO(handler, "Starting trading pipeline");

        // init position snapshot here after incremental request
        auto posRequest = fixBuilder.requestForPositions(nextSeqNum);
        sendMsg(posRequest);

        while (isRunning)
        {
            try
            {
                if (!socket.available())
                {
                    auto mdRequest = fixBuilder.marketDataRequestTopLevel(nextSeqNum, config->instrument);
                    sendMsg(mdRequest);
                }

                // 18ms RTT on average
                auto reader = recvMsg();

                [[maybe_unused]] auto timer = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");

                // test request
                if (reader.isMessageType("1"))
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum, reader.getStringView("112"));
                    sendMsg(msg);
                    STABLEARB_LOG_INFO(handler, "Received TestRequest, sending Heartbeat");
                    continue;
                }

                // heartbeat
                if (reader.isMessageType("0"))
                {
                    STABLEARB_LOG_INFO(handler, "Received Heartbeat");
                    continue;
                }

                // execution report
                if (reader.isMessageType("8"))
                {
                    handler->invoke(tag::Quoter::ExecutionReport{}, std::move(reader));
                    continue;
                }

                // market data update
                if (reader.isMessageType("W"))
                {
                    handler->invoke(tag::Quoter::Quote{}, std::move(reader), nextSeqNum);
                    continue;
                }

                // position update
                if (reader.isMessageType("AP"))
                {
                    handler->invoke(tag::Risk::UpdatePosition{}, std::move(reader));
                    continue;
                }
            }
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

        auto msg = fixBuilder.login(nextSeqNum, config->username, config->secret, 5);
        sendMsg(msg);
        auto reader = recvMsg();

        STABLEARB_LOG_VERIFY(handler, reader.isMessageType("A"), "Login unsuccessful");
        STABLEARB_LOG_INFO(handler, "Login successful");
    }

    inline FIXReader recvMsg()
    {
        io::read_until(socket, recvBuffer, boost::regex("10=\\d+\\x01"));
        auto const* data = boost::asio::buffer_cast<char const*>(recvBuffer.data());
        auto const size = recvBuffer.size();
        std::string_view str{data, size};

        FIXReader reader{str};
        STABLEARB_LOG_VERIFY(this->getHandler(), (!reader.isMessageType("3")), "Reject message received", str);
        recvBuffer.consume(size);
        return std::move(reader);
    }

    inline void sendMsg(std::string_view msg)
    {
        spinThrottle();
        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        STABLEARB_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNum;
    }

    inline void spinThrottle()
    {
        auto nextAllowed = lastSent + interval;
        while (std::chrono::steady_clock::now() <= nextAllowed)
            _mm_pause();

        lastSent = std::chrono::steady_clock::now();
    }

    io::io_context ioContext;
    io::ip::tcp::socket socket{ioContext};
    io::streambuf recvBuffer;

    bool isRunning = false;
    std::size_t nextSeqNum = 1u;

    FIXMessageBuilder fixBuilder;

    std::chrono::steady_clock::time_point lastSent = std::chrono::steady_clock::now();
    std::chrono::milliseconds const interval{200};
};

} // namespace stablearb
