#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/tags.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
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

        forceSendMsg(fixBuilder.logout(nextSeqNum));
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

            socket.set_option(io::ip::tcp::no_delay(true));

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

    template<typename... Orders>
    [[gnu::hot, gnu::always_inline]]
    inline bool handle(tag::Stream::TakeMarketOrders, Orders&&... orders)
    {
        auto* config = this->getConfig();
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(handler, "Taking market orders");

        auto nextAllowed = lastSent + interval;
        if (msgCountInterval <= 5u - sizeof...(orders))
            msgCountInterval += sizeof...(orders);
        else if (std::chrono::steady_clock::now() >= nextAllowed)
        {
            lastSent = std::chrono::steady_clock::now();
            msgCountInterval = sizeof...(orders);
        }
        else
        { 
            PHOENIX_LOG_WARN(handler, "Orders rejected with msgCountInterval", msgCountInterval);
            return false;
        }

        auto const sendOrder = [this, handler](SingleOrder<Traits> const& order)
        {
            PHOENIX_LOG_INFO(
                handler, "Sending order", order.symbol, order.volume.asDouble(), order.side == 1 ? "BID" : "ASK");

            if (order.isLimit)
            {
                std::string_view msg = fixBuilder.newOrderSingle(nextSeqNum, order.symbol, order);
                sendUnthrottled(msg);
            }
            else
            {
                std::string_view msg = fixBuilder.newMarketOrderSingle(nextSeqNum, order);
                sendUnthrottled(msg);
            }
        };

        (sendOrder(orders), ...);

        return true;
    }

    double handle(tag::Stream::GetBalance, std::string_view currency)
    {
        auto msg = fixBuilder.userRequest(nextSeqNum, currency, this->getConfig()->username);
        forceSendMsg(msg);
        auto reader = forceReadMsg();
        PHOENIX_LOG_VERIFY(this->getHandler(), reader.isMessageType("BF"), "Invalid message type");
        return reader.template getNumber<double>("100001");
    }

private:
    void startPipeline()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto const& instrumentList = config->instrumentList;
        auto const& instrumentMap = config->instrumentMap;

        PHOENIX_LOG_INFO(handler, "Starting trading pipeline");

        handler->invoke(tag::Hitter::InitBalances{});

        // initializing snapshots
        for (auto const& instrument : instrumentList)
            getSnapshot(instrument);

        unsigned int i = 0u;
        while (i < 3u)
        {
            auto reader = forceReadMsg();
            if (reader.isMessageType("W"))
            {
                handler->invoke(tag::Hitter::MDUpdate{}, std::move(reader), false);
                ++i;
            }
            else
                PHOENIX_LOG_FATAL(handler, "Unknown message type", reader.getMessageType());
        }

        // subscribing to incremental
        for (auto const& instrument : instrumentList)
            subscribeToOne(instrument);

        while (isRunning)
        {
            [[maybe_unused]] auto profiler = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");
            try
            {
                if (std::chrono::steady_clock::now() - heartbeatLastSent > HEARTBEAT_INTERVAL) [[unlikely]]
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum);
                    forceSendMsg(msg);
                    PHOENIX_LOG_DEBUG(handler, "Sent heartbeat");
                }

                auto readerOpt = recvMsg();
                if (!readerOpt)
                    continue;

                auto& reader = *readerOpt;

                auto const& msgType = reader.getMessageType();

                // test request
                if (msgType == "1")
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum, reader.getStringView("112"));
                    trySendMsg(msg);
                    PHOENIX_LOG_DEBUG(handler, "Received TestRequest, sending Heartbeat");
                    continue;
                }

                auto const& recvInstrument = reader.getString("55");
                if (!instrumentMap.contains(recvInstrument))
                    continue;

                // market data update
                if (msgType == "X" or msgType == "W") [[likely]]
                {
                    handler->invoke(tag::Hitter::MDUpdate{}, std::move(reader), true);
                    continue;
                }

                // execution report
                if (msgType == "8")
                {
                    handler->invoke(tag::Hitter::ExecutionReport{}, std::move(reader));
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

    void getSnapshot(std::string_view instrument)
    {
        std::string_view const msg = fixBuilder.marketDataRequestTopLevel(nextSeqNum, instrument);
        forceSendMsg(msg);
    }

    void subscribeToOne(std::string_view instrument)
    {
        std::string_view const msg = fixBuilder.marketDataRefreshSingle(nextSeqNum, instrument);
        forceSendMsg(msg);
    }

    void subscribeToAll(std::vector<std::string> const& instruments)
    {
        std::string_view const mdRequest = fixBuilder.marketDataRefreshTriple(nextSeqNum, instruments);
        forceSendMsg(mdRequest);
    }

    void login()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto msg = fixBuilder.login(nextSeqNum, config->username, config->secret, 30);
        forceSendMsg(msg);
        auto reader = forceReadMsg();
        PHOENIX_LOG_VERIFY(handler, reader.isMessageType("A"), "Login unsuccessful with message type", reader.getMessageType());
        PHOENIX_LOG_INFO(handler, "Login successful");
    }

    [[gnu::hot, gnu::always_inline]]
    inline std::optional<FIXReader> recvMsg()
    {
        if (!socket.available())
            return {};

        auto const size = io::read_until(socket, recvBuffer, boost::regex("\\x0110=\\d+\\x01"));
        auto const* data = boost::asio::buffer_cast<char const*>(recvBuffer.data());
        std::string_view str{data, size};
        FIXReader reader{str};
        PHOENIX_LOG_VERIFY(this->getHandler(), (!reader.isMessageType("3")), "Reject message received", str);
        recvBuffer.consume(size);
        return {std::move(reader)};
    }

    [[gnu::hot, gnu::always_inline]]
    inline FIXReader forceReadMsg()
    {
        for (;;)
        {
            auto reader = recvMsg();
            if (reader)
                return std::move(*reader);
        }
    }

    [[gnu::hot, gnu::always_inline]]
    inline void forceSendMsg(std::string_view msg)
    {
        while (!trySendMsg(msg))
            _mm_pause();
    }

    // non-blocking for throttler, preventing waiting for next window with stale orders
    [[gnu::hot, gnu::always_inline]]
    inline bool trySendMsg(std::string_view msg)
    {
        auto nextAllowed = lastSent + interval;
        if (std::chrono::steady_clock::now() >= nextAllowed)
        {
            lastSent = std::chrono::steady_clock::now();
            msgCountInterval = 1u;
        }
        else if (msgCountInterval < 5u)
            ++msgCountInterval;
        else
            return false;

        sendUnthrottled(msg);
        return true;
    }

    [[gnu::hot, gnu::always_inline]]
    inline void sendUnthrottled(std::string_view msg)
    {
        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        PHOENIX_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNum;
    }

    io::io_context ioContext;
    io::streambuf recvBuffer;
    std::size_t nextSeqNum = 1u;
    io::ip::tcp::socket socket{ioContext};

    static constexpr std::chrono::seconds HEARTBEAT_INTERVAL{25u};
    std::chrono::steady_clock::time_point heartbeatLastSent = std::chrono::steady_clock::now();

    bool isRunning = false;

    FIXMessageBuilder fixBuilder;

    std::chrono::steady_clock::time_point lastSent = std::chrono::steady_clock::now();
    std::chrono::seconds const interval{1u};
    std::uint64_t msgCountInterval = 0u;
};

} // namespace phoenix::triangular
