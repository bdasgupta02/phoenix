#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/quotes.hpp"
#include "phoenix/tags.hpp"

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

namespace phoenix::triangular {

// risk:
// all orders are fill or kill
// send buy first, then sell
// if sell is cancelled, retry the best bid/ask to cut losses
// if rejected, fatal

// 200ms is a big risk
// rather use a 4 burst, so throttle 500ms (under 5/s limit and 20 burst)

// send 3 order messages at once (v2 with OVH)

template<typename NodeBase>
struct Stream : NodeBase
{
    using Traits = NodeBase::Traits;

    Stream(auto const& config, auto& handler)
        : NodeBase{config, handler}
        , fixBuilder(config.client)
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
        trySendMsg(fixBuilder.logout(nextSeqNum));

        boost::system::error_code ec;
        socket.close(ec);
        if (ec)
            PHOENIX_LOG_ERROR(handler, "Error closing socket", ec.message());
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
            PHOENIX_LOG_INFO(handler, "Connected successfully");
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

private:
    void startPipeline()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto const& instrumentList = config->instrumentList;
        auto const& instrumentSet = config->instrumentSet;

        PHOENIX_LOG_INFO(handler, "Starting trading pipeline");

        for (auto const& instrument : instrumentList)
            subscribe(instrument);

        while (isRunning)
        {
            try
            {
                auto reader = recvMsg();

                [[maybe_unused]] auto timer = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");

                auto const& recvInstrument = reader.getString("55");
                if (recvInstrument != FIXReader::UNKNOWN && !instrumentSet.contains(recvInstrument))
                {
                    PHOENIX_LOG_DEBUG(handler, "Message received for other instrument", reader.getStringView("55"));
                    continue;
                };

                // execution report
                if (reader.isMessageType("8"))
                {
                    handler->invoke(tag::Hitter::ExecutionReport{}, std::move(reader));
                    continue;
                }

                // market data update
                if (reader.isMessageType("W"))
                {
                    handler->invoke(tag::Hitter::Hit{}, std::move(reader));
                    continue;
                }

                // test request
                if (reader.isMessageType("1"))
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum, reader.getStringView("112"));
                    trySendMsg(msg);
                    PHOENIX_LOG_DEBUG(handler, "Received TestRequest, sending Heartbeat");
                    continue;
                }

                // heartbeat
                if (reader.isMessageType("0"))
                {
                    PHOENIX_LOG_DEBUG(handler, "Received Heartbeat");
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

    void subscribe(std::string_view instrument)
    {
        std::string_view const mdRequest = fixBuilder.marketDataRequestIncremental(nextSeqNum, instrument);
        while (!trySendMsg(mdRequest))
            _mm_pause();

        auto reader = recvMsg();
        PHOENIX_LOG_VERIFY(
            this->getHandler(),
            (!reader.isMessageType("Y")),
            "Invalid market data request",
            reader.getStringView("58"));
    }

    void login()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto msg = fixBuilder.login(nextSeqNum, config->username, config->secret, 30);
        while (!trySendMsg(msg))
            _mm_pause();

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

    inline bool trySendMsg(std::string_view msg)
    {
        auto nextAllowed = lastSent + interval;
        if (std::chrono::steady_clock::now() <= nextAllowed)
        {
            lastSent = std::chrono::steady_clock::now();
            msgCountInterval = 1u;
        }
        else if (msgCountInterval < 5u)
            ++msgCountInterval;
        else
            return false;

        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        PHOENIX_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNum;
        return true;
    }

    io::io_context ioContext;
    io::ip::tcp::socket socket{ioContext};
    io::streambuf recvBuffer;

    bool isRunning = false;
    std::size_t nextSeqNum = 1u;

    FIXMessageBuilder fixBuilder;

    std::chrono::steady_clock::time_point lastSent = std::chrono::steady_clock::now();
    std::chrono::seconds const interval{1u};
    std::uint64_t msgCountInterval = 0u;
};

} // namespace phoenix::triangular
