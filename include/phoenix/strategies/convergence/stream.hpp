#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/tags.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>

#include <immintrin.h>

namespace {
namespace io = ::boost::asio;
} // namespace

namespace phoenix::convergence {

// Single-threaded TCP stream for Deribit with FIX, specifically for convergence where liquidity is low
template<typename NodeBase>
struct Stream : NodeBase
{
    using Traits = NodeBase::Traits;
    using PriceType = NodeBase::Traits::PriceType;

    Stream(auto const& config, auto& handler)
        : NodeBase{config, handler}
        , fixBuilder(config.client)
        , heartbeatTimer(&Stream::heartbeatWorker, this)
    {
        recvBuffer.prepare(8192u);
    }

    void handle(tag::Stream::Stop)
    {
        if (!isRunning)
            return;

        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(handler, "Stopping stream");
        isRunning = false;
        sendMsg(fixBuilder.logout(nextSeqNum));

        boost::system::error_code ec;
        socket.close(ec);
        if (ec)
            PHOENIX_LOG_ERROR(handler, "Error closing socket", ec.message());

        stopHeartbeat.test_and_set();
        heartbeatTimer.join();
    }

    void handle(tag::Stream::Start)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        try
        {
            if (config->colo)
            {
                int port = std::atoi(config->port.c_str());
                io::ip::tcp::endpoint endpoint(io::ip::address::from_string(config->host), port);
                socket.connect(endpoint);
            }
            else
            {
                io::ip::tcp::resolver resolver{ioContext};
                auto endpoints = resolver.resolve(config->host, config->port);
                io::connect(socket, endpoints);
            }
            PHOENIX_LOG_INFO(handler, "Connected successfully");
            socket.set_option(io::ip::tcp::no_delay(true));
            isRunning = true;
        }
        catch (std::exception const& e)
        {
            PHOENIX_LOG_ERROR(handler, "Connection error", e.what());
            return;
        }

        login();
        startPipeline();
    }

    inline void handle(tag::Stream::SendQuote, SingleOrder<Traits>& quote)
    {
        auto msg = fixBuilder.newOrderSingle(nextSeqNum, this->getConfig()->instrument, quote);
        sendMsg(msg);
    }

    inline void handle(tag::Stream::CancelQuote, std::string_view orderId)
    {
        auto msg = fixBuilder.orderCancelRequest(nextSeqNum, this->getConfig()->instrument, orderId);
        sendMsg(msg);
    }

private:
    inline void startPipeline()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();
        auto const& instrument = config->instrument;

        PHOENIX_LOG_INFO(handler, "Starting trading pipeline");

        while (isRunning)
        {
            try
            {
                if (sendHeartbeat.test()) [[unlikely]]
                {
                    auto blankHeartbeat = fixBuilder.heartbeat(nextSeqNum);
                    sendMsg(blankHeartbeat);
                    PHOENIX_LOG_DEBUG(handler, "Sent heartbeat");
                    sendHeartbeat.clear();
                }

                handler->invoke(tag::Risk::Check{});

                if (!socket.available() && recvBuffer.size() == 0u)
                {
                    auto mdRequest = fixBuilder.marketDataRequestTopLevel(nextSeqNum, instrument);
                    sendMsg(mdRequest);
                }

                // 18ms RTT on average
                auto reader = recvMsg();

                [[maybe_unused]] auto timer = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");

                PHOENIX_LOG_VERIFY(
                    handler, (!reader.isMessageType("Y")), "Invalid market data request", reader.getStringView("58"));

                if (reader.contains("55") && reader.getStringView("55") != instrument)
                {
                    PHOENIX_LOG_DEBUG(handler, "Message received for other instrument", reader.getStringView("55"));
                    continue;
                };

                // test request
                if (reader.isMessageType("1"))
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum, reader.getStringView("112"));
                    sendMsg(msg);
                    PHOENIX_LOG_DEBUG(handler, "Received TestRequest, sending Heartbeat");
                    continue;
                }

                // heartbeat
                if (reader.isMessageType("0"))
                {
                    PHOENIX_LOG_DEBUG(handler, "Received Heartbeat");
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
                    handler->invoke(tag::Quoter::MDUpdate{}, std::move(reader));
                    continue;
                }
            }
            catch (std::exception const& e)
            {
                PHOENIX_LOG_FATAL(handler, "Error in trading pipeline", e.what());
                break;
            }
        }
    }

    inline void login()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto msg = fixBuilder.login(nextSeqNum, config->username, config->secret, 30);
        sendMsg(msg);
        auto reader = recvMsg();

        PHOENIX_LOG_VERIFY(handler, reader.isMessageType("A"), "Login unsuccessful");
        PHOENIX_LOG_INFO(handler, "Login successful");
    }

    inline FIXReader recvMsg()
    {
        auto const size = io::read_until(socket, recvBuffer, boost::regex("\\x0110=\\d+\\x01"));
        auto const* data = boost::asio::buffer_cast<char const*>(recvBuffer.data());
        std::string_view str{data, size};
        FIXReader reader{str};
        PHOENIX_LOG_VERIFY(this->getHandler(), (!reader.isMessageType("3")), "Reject message received", str);
        recvBuffer.consume(size);
        return std::move(reader);
    }

    inline void sendMsg(std::string_view msg)
    {
        spinThrottle();
        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        PHOENIX_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNum;
    }

    inline void spinThrottle()
    {
        auto nextAllowed = lastSent + interval;
        while (std::chrono::steady_clock::now() <= nextAllowed)
            _mm_pause();

        lastSent = std::chrono::steady_clock::now();
    }

    void heartbeatWorker()
    {
        while (!stopHeartbeat.test())
        {
            std::this_thread::sleep_for(HEARTBEAT_INTERVAL);
            sendHeartbeat.test_and_set();
        }
    }

    io::io_context ioContext;
    io::ip::tcp::socket socket{ioContext};
    io::streambuf recvBuffer;

    std::thread heartbeatTimer;
    std::atomic_flag sendHeartbeat = ATOMIC_FLAG_INIT;
    std::atomic_flag stopHeartbeat = ATOMIC_FLAG_INIT;
    static constexpr std::chrono::seconds HEARTBEAT_INTERVAL{25u};

    bool isRunning = false;
    std::size_t nextSeqNum = 1u;

    FIXMessageBuilder fixBuilder;

    std::chrono::steady_clock::time_point lastSent = std::chrono::steady_clock::now();
    std::chrono::milliseconds const interval{200u};
};

} // namespace phoenix::convergence
