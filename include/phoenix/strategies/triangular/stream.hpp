#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/common/profiler.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/tools/fix_circular_buffer.hpp"
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
#include <linux/socket.h>

namespace {
namespace io = ::boost::asio;
} // namespace

namespace phoenix::triangular {

template<typename NodeBase>
struct Stream : NodeBase
{
    using Traits = NodeBase::Traits;

    Stream(auto const& config, auto& handler)
        : NodeBase{config, handler}
        , fixBuilder(config.client)
    {}

    void handle(tag::Stream::Stop)
    {
        auto logoutMsg = fixBuilder.logout(nextSeqNum);
        isRunning = false;
        this->getHandler()->invoke(tag::TCPSocket::Stop{}, logoutMsg);
    }

    void handle(tag::Stream::Start)
    {
        auto* config = this->getConfig();
        this->getHandler()->invoke(tag::TCPSocket::Connect{}, config->host, config->port, config->colo); 
        login();
        isRunning = true;

        startPipeline();
    }

    template<typename... Orders>
    [[gnu::hot, gnu::always_inline]]
    inline bool handle(tag::Stream::TakeMarketOrders, Orders&&... orders)
    {
        auto* handler = this->getHandler();
        if (!handler->retrieve(tag::TCPSocket::CheckThrottle{}, sizeof...(Orders)))
            return false;

        auto const sendOrder = [&](auto const& order)
        {
            auto msg = fixBuilder.newOrderSingle(nextSeqNum, order.symbol, order);
            handler->invoke(tag::TCPSocket::SendUnthrottled{}, msg);
            ++nextSeqNum;
        };

        (sendOrder(orders), ...);
        return true;
    }

    [[gnu::hot, gnu::always_inline]]
    inline bool handle(tag::Stream::TakeMarketOrders, auto const& order)
    {
        auto msg = fixBuilder.newOrderSingle(nextSeqNum, order.symbol, order);

        bool const success = this->getHandler()->retrieve(tag::TCPSocket::Send{}, msg);
        if (success) [[likely]]
            ++nextSeqNum;

        return success;
    }

    [[gnu::hot, gnu::always_inline]]
    inline bool handle(tag::Stream::CancelQuote, std::string_view symbol, std::string_view orderId)
    {
        auto msg = fixBuilder.orderCancelRequest(nextSeqNum, symbol, orderId);

        bool const success = this->getHandler()->retrieve(tag::TCPSocket::Send{}, msg);
        if (success) [[likely]]
            ++nextSeqNum;

        return success;
    }

private:
    void startPipeline()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto const& instrumentList = config->instrumentList;
        auto const& instrumentMap = config->instrumentMap;

        PHOENIX_LOG_INFO(handler, "Starting trading pipeline");

        // initializing snapshots
        for (auto const& instrument : instrumentList)
            getSnapshot(instrument);

        unsigned int i = 0u;
        while (i < 3u)
        {
            auto msg = handler->retrieve(tag::TCPSocket::ForceReceive{});
            FIXReader reader{msg};
            if (reader.isMessageType("W"))
            {
                handler->invoke(tag::Hitter::MDUpdate{}, std::move(reader), false);
                ++i;
            }
            else
                PHOENIX_LOG_FATAL(handler, "Unknown message type", reader.getMessageType());
        }

        // subscribing to incremental
        subscribeToAll(instrumentList);

        while (isRunning)
        {
            [[maybe_unused]] auto profiler = handler->retrieve(tag::Profiler::Guard{}, "Trading pipeline");
            try
            {
                if (std::chrono::steady_clock::now() - heartbeatLastSent > HEARTBEAT_INTERVAL) [[unlikely]]
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum);
                    handler->invoke(tag::TCPSocket::ForceSend{}, msg);
                    ++nextSeqNum;
                    heartbeatLastSent = std::chrono::steady_clock::now();
                }

                auto msgOpt = handler->retrieve(tag::TCPSocket::Receive{});
                if (!msgOpt)
                    continue;

                FIXReader reader{*msgOpt};
                auto const& msgType = reader.getMessageType();

                // test request
                if (msgType == "1")
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum, reader.getStringView("112"));
                    handler->invoke(tag::TCPSocket::ForceSend{}, msg);
                    ++nextSeqNum;
                    PHOENIX_LOG_INFO(handler, "Received TestRequest, sending Heartbeat");
                    continue;
                }

                auto const& recvInstrument = reader.getString("55");
                if (!instrumentMap.contains(recvInstrument))
                    continue;

                if (msgType == "X" or msgType == "W") [[likely]]
                {
                    handler->invoke(tag::Hitter::MDUpdate{}, std::move(reader), true);
                    continue;
                }
                else if (msgType == "8")
                {
                    handler->invoke(tag::Hitter::ExecutionReport{}, std::move(reader));
                    continue;
                }
                else
                {
                    PHOENIX_LOG_INFO(handler, "Unknown message type");
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
        this->getHandler()->invoke(tag::TCPSocket::ForceSend{}, msg);
        ++nextSeqNum;
    }

    void subscribeToOne(std::string_view instrument)
    {
        std::string_view const msg = fixBuilder.marketDataRefreshSingle(nextSeqNum, instrument);
        this->getHandler()->invoke(tag::TCPSocket::ForceSend{}, msg);
        ++nextSeqNum;
    }

    void subscribeToAll(std::vector<std::string> const& instruments)
    {
        std::string_view const msg = fixBuilder.marketDataRefreshTriple(nextSeqNum, instruments);
        this->getHandler()->invoke(tag::TCPSocket::ForceSend{}, msg);
        ++nextSeqNum;
    }

    void login()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto msg = fixBuilder.login(nextSeqNum, config->username, config->secret, 120);
        handler->invoke(tag::TCPSocket::ForceSend{}, msg);
        ++nextSeqNum;

        auto recvMsg = handler->retrieve(tag::TCPSocket::ForceReceive{});
        FIXReader reader{recvMsg};
        PHOENIX_LOG_VERIFY(handler, reader.isMessageType("A"), "Login unsuccessful with message type", reader.getMessageType());
        PHOENIX_LOG_INFO(handler, "Login successful");
    }

    bool isRunning = false;
    std::size_t nextSeqNum = 1u;
    FIXMessageBuilder fixBuilder;
    static constexpr std::chrono::seconds HEARTBEAT_INTERVAL{80u};
    std::chrono::steady_clock::time_point heartbeatLastSent = std::chrono::steady_clock::now();
};

} // namespace phoenix::triangular
