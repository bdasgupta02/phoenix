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
#include <optional>
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
        , fixBuilder(config.client)
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
        auto const& instrument = config->instrument;

        STABLEARB_LOG_INFO(handler, "Starting trading pipeline");

        while (isRunning)
        {
            try
            {
                if (!socket.available() && recvBuffer.size() == 0u)
                {
                    auto mdRequest = fixBuilder.marketDataRequestTopLevel(nextSeqNum, instrument);
                    sendMsg(mdRequest);
                }

                // 18ms RTT on average
                auto reader = recvMsg();

                [[maybe_unused]] auto timer = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");

                if (reader.contains("55") && reader.getStringView("55") != instrument)
                    continue;

                // test request
                if (reader.isMessageType("1"))
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum, reader.getStringView("112"));
                    sendMsg(msg);
                    STABLEARB_LOG_DEBUG(handler, "Received TestRequest, sending Heartbeat");
                    continue;
                }

                // heartbeat
                if (reader.isMessageType("0"))
                {
                    STABLEARB_LOG_DEBUG(handler, "Received Heartbeat");
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

        auto msg = fixBuilder.login(nextSeqNum, config->username, config->secret, 30);
        sendMsg(msg);
        auto reader = recvMsg();

        STABLEARB_LOG_VERIFY(handler, reader.isMessageType("A"), "Login unsuccessful");
        STABLEARB_LOG_INFO(handler, "Login successful");
    }

    inline FIXReader recvMsg()
    {
        // previously read message
        if (recvBuffer.size() > 0u)
        {
            auto reader = getFirstMsgFromBuffer();
            if (reader)
                return std::move(*reader);
        }

        // guarantee of a SOH present inside buffer, but verifying just in case
        io::read_until(socket, recvBuffer, boost::regex("10=\\d+\\x01"));
        auto reader = getFirstMsgFromBuffer();
        STABLEARB_LOG_VERIFY(this->getHandler(), (reader != std::nullopt), "Invalid message from TCP stream");
        return std::move(*reader);
    }

    inline std::optional<FIXReader> getFirstMsgFromBuffer()
    {
        char const* data = boost::asio::buffer_cast<char const*>(recvBuffer.data());
        std::int64_t firstMsgSize = findMsgSize(data);
        if (firstMsgSize < 0)
            return {};

        std::string_view str{data, static_cast<std::size_t>(firstMsgSize)};
        FIXReader reader{str};
        STABLEARB_LOG_VERIFY(this->getHandler(), (!reader.isMessageType("3")), "Reject message received", str);
        recvBuffer.consume(firstMsgSize);
        return {std::move(reader)};
    }

    inline std::int64_t findMsgSize(char const* data)
    {
        std::int64_t firstMsgSize = -1;
        std::int64_t const bufferSize = recvBuffer.size();
        for (std::int64_t i = 0; i < bufferSize - 3; ++i)
        {
            if (data[i] == '1' && data[i + 1] == '0' && data[i + 2] == '=')
            {
                for (size_t j = i + 3; j < bufferSize; ++j)
                {
                    if (data[j] == '\x01')
                    {
                        firstMsgSize = j + 1;
                        break;
                    }
                }
            }
        }
        return firstMsgSize;
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
