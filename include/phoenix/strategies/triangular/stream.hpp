#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/tags.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <thread>
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

        for (unsigned i = 0u; i < 3u; ++i)
        {
            socketIdx = ++socketIdx % 3u;
            forceSendMsg(fixBuilder.logout(nextSeqNums[socketIdx]));
            boost::system::error_code ec;
            sockets[socketIdx].close(ec);
            if (ec)
                PHOENIX_LOG_ERROR(handler, "Error closing socket", ec.message());
        }

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
                for (unsigned i = 0u; i < 3u; ++i)
                {
                    int port = std::atoi(config->port.c_str());
                    io::ip::tcp::endpoint endpoint(io::ip::address::from_string(config->host), port);
                    sockets[i].connect(endpoint);
                }
            }
            else
            {
                for (unsigned i = 0u; i < 3u; ++i)
                {
                    io::ip::tcp::resolver resolver{ioContext};
                    auto endpoints = resolver.resolve(config->host, config->port);
                    io::connect(sockets[i], endpoints);
                }
            }

            for (unsigned i = 0u; i < 3u; ++i)
                sockets[i].set_option(io::ip::tcp::no_delay(true));

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
                std::string_view msg = fixBuilder.newOrderSingle(nextSeqNums[socketIdx], order.symbol, order);
                sendUnthrottled(msg);
            }
            else
            {
                std::string_view msg = fixBuilder.newMarketOrderSingle(nextSeqNums[socketIdx], order);
                sendUnthrottled(msg);
            }
        };

        (sendOrder(orders), ...);

        return true;
    }

    double handle(tag::Stream::GetBalance, std::string_view currency)
    {
        auto msg = fixBuilder.userRequest(nextSeqNums[socketIdx], currency, this->getConfig()->username);
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
                PHOENIX_LOG_INFO(handler, "Unknown message type", reader.getMessageType());
        }

        // subscribing to incremental
        for (auto const& instrument : instrumentList)
        {
            socketIdx = ++socketIdx % 3u;
            subscribeToOne(instrument);
        }

        while (isRunning)
        {
            [[maybe_unused]] auto profiler = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");
            socketIdx = ++socketIdx % 3u;
            try
            {
                if (sendHeartbeat.test()) [[unlikely]]
                {
                    auto blankHeartbeat = fixBuilder.heartbeat(nextSeqNums[socketIdx]);
                    forceSendMsg(blankHeartbeat);
                    PHOENIX_LOG_DEBUG(handler, "Sent heartbeat");
                    sendHeartbeat.clear();
                }

                auto readerOpt = recvMsg();
                if (!readerOpt)
                    continue;

                auto& reader = *readerOpt;

                auto const& msgType = reader.getMessageType();

                // test request
                if (msgType == "1")
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNums[socketIdx], reader.getStringView("112"));
                    trySendMsg(msg);
                    PHOENIX_LOG_DEBUG(handler, "Received TestRequest, sending Heartbeat");
                    continue;
                }

                auto const& recvInstrument = reader.getString("55");
                if (recvInstrument != instrumentList[socketIdx])
                    continue;

                // market data update
                if (msgType == "X" or msgType == "W") [[likely]]
                {
                    [[maybe_unused]] auto mdProfiler = handler->retrieve(tag::Profiler::Guard{}, "MDProfiler");
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
        std::string_view const msg = fixBuilder.marketDataRequestTopLevel(nextSeqNums[socketIdx], instrument);
        forceSendMsg(msg);
    }

    void subscribeToOne(std::string_view instrument)
    {
        std::string_view const msg = fixBuilder.marketDataRefreshSingle(nextSeqNums[socketIdx], instrument);
        forceSendMsg(msg);
    }

    void subscribeToAll(std::vector<std::string> const& instruments)
    {
        std::string_view const mdRequest = fixBuilder.marketDataRefreshTriple(nextSeqNums[socketIdx], instruments);
        forceSendMsg(mdRequest);
    }

    void login()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        for (unsigned i = 0u; i < 3u; ++i)
        {
            socketIdx = ++socketIdx % 3u;
            auto msg = fixBuilder.login(nextSeqNums[socketIdx], config->username, config->secret, 30);
            forceSendMsg(msg);
            auto reader = forceReadMsg();
            PHOENIX_LOG_VERIFY(handler, reader.isMessageType("A"), "Login unsuccessful with message type", reader.getMessageType(), "with socket", socketIdx, "and seq", nextSeqNums[socketIdx]);
            PHOENIX_LOG_INFO(handler, "Login successful for socket", socketIdx);
        }

        PHOENIX_LOG_INFO(handler, "All logins successful");
    }

    [[gnu::hot, gnu::always_inline]]
    inline std::optional<FIXReader> recvMsg()
    {
        auto& socket = sockets[socketIdx];
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
        io::write(sockets[socketIdx], io::buffer(msg), error);
        PHOENIX_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNums[socketIdx];
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
    io::streambuf recvBuffer;
    unsigned socketIdx = 2u;
    std::array<std::size_t, 3u> nextSeqNums{1u, 1u, 1u};
    std::array<io::ip::tcp::socket, 3u> sockets{
        io::ip::tcp::socket{ioContext}, io::ip::tcp::socket{ioContext}, io::ip::tcp::socket{ioContext}};

    std::thread heartbeatTimer;
    std::atomic_flag sendHeartbeat = ATOMIC_FLAG_INIT;
    std::atomic_flag stopHeartbeat = ATOMIC_FLAG_INIT;
    static constexpr std::chrono::seconds HEARTBEAT_INTERVAL{25u};

    bool isRunning = false;

    FIXMessageBuilder fixBuilder;

    std::chrono::steady_clock::time_point lastSent = std::chrono::steady_clock::now();
    std::chrono::seconds const interval{1u};
    std::uint64_t msgCountInterval = 0u;
};

} // namespace phoenix::triangular
