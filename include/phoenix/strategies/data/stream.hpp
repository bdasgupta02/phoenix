#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/tags.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <thread>

#include <immintrin.h>

namespace {
namespace io = ::boost::asio;
} // namespace

namespace phoenix::data {

template<typename NodeBase>
struct Stream : NodeBase
{
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
        isRunning = false;
        forceSendMsg(fixBuilder.logout(nextSeqNum));
        boost::system::error_code ec;
        socket.close(ec);
        if (ec)
            PHOENIX_LOG_FATAL(handler, "Error closing socket", ec.message());
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

            // nalges sucks for this
            socket.set_option(io::ip::tcp::no_delay(true));
            isRunning = true;
        }
        catch (std::exception const& e)
        {
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
        
        PHOENIX_LOG_CSV(handler, "instrument", "time", "microseconds", "bid", "ask", "index");

        for (auto const& instrument : config->instruments)
            getSnapshot(instrument);

        unsigned int i = 0u;
        while (i < 3u)
        {
            auto reader = recvMsg();
            if (reader.isMessageType("W"))
            {
                auto const& recvInstrument = reader.getString("55");
                auto now = std::chrono::system_clock::now();
                auto timeT = std::chrono::system_clock::to_time_t(now);
                auto durationSinceEpoch = now.time_since_epoch();
                auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(durationSinceEpoch) % 1000000;
                auto timeStr = std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");

                std::string_view newBid;
                std::string_view newAsk;

                for (std::size_t i = 0u; i < 2u; ++i)
                {
                    unsigned const typeField = reader.template getNumber<unsigned>("269", i);
                    if (typeField == 0u)
                        newBid = reader.getStringView("270", i);
                    if (typeField == 1u)
                        newAsk = reader.getStringView("270", i);
                }

                std::string_view newIndex = reader.getStringView("100090");
                PHOENIX_LOG_CSV(handler, recvInstrument, microseconds.count(), timeStr, newBid, newAsk, newIndex);
                ++i;
            }
            else
                PHOENIX_LOG_FATAL(handler, "Unknown message type", reader.getMessageType());
        }

        subscribeToAll(config->instruments);

        while (isRunning)
        {
            try
            {
                if (std::chrono::steady_clock::now() - heartbeatLastSent > HEARTBEAT_INTERVAL) [[unlikely]]
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum);
                    forceSendMsg(msg);
                }

                auto reader = recvMsg();
                auto const& msgType = reader.getMessageType();

                // test request
                if (msgType == "1")
                {
                    auto msg = fixBuilder.heartbeat(nextSeqNum, reader.getStringView("112"));
                    forceSendMsg(msg);
                    continue;
                }

                // wrong instrument
                auto const& recvInstrument = reader.getString("55");

                // market data update
                if (msgType == "W" or msgType == "X") [[likely]]
                {
                    auto now = std::chrono::system_clock::now();
                    auto timeT = std::chrono::system_clock::to_time_t(now);
                    auto durationSinceEpoch = now.time_since_epoch();
                    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(durationSinceEpoch) % 1000000;
                    auto timeStr = std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");

                    std::string_view newBid;
                    std::string_view newAsk;

                    for (std::size_t i = 0u; i < 2u; ++i)
                    {
                        unsigned const typeField = reader.template getNumber<unsigned>("269", i);
                        if (typeField == 0u)
                            newBid = reader.getStringView("270", i);
                        if (typeField == 1u)
                            newAsk = reader.getStringView("270", i);
                    }

                    std::string_view newIndex = reader.getStringView("100090");

                    PHOENIX_LOG_CSV(handler, recvInstrument, microseconds.count(), timeStr, newBid, newAsk, newIndex);
                    continue;
                }

                // MD reject
                else if (msgType == "Y") [[unlikely]]
                {
                    PHOENIX_LOG_FATAL(handler, "MD reject message received");
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

    void subscribeToAll(std::vector<std::string> const& instruments)
    {
        std::string_view const msg = fixBuilder.marketDataRefreshTriple(nextSeqNum, instruments);
        forceSendMsg(msg);
    }

    void getSnapshot(std::string_view instrument)
    {
        std::string_view const msg = fixBuilder.marketDataRequestTopLevel(nextSeqNum, instrument);
        forceSendMsg(msg);
    }

    void login()
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto msg = fixBuilder.login(nextSeqNum, config->username, config->secret, 30);
        forceSendMsg(msg);

        auto reader = recvMsg();
    }

    [[gnu::hot, gnu::always_inline]]
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

    [[gnu::hot, gnu::always_inline]]
    inline void sendUnthrottled(std::string_view msg)
    {
        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        PHOENIX_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
        ++nextSeqNum;
    }

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
    inline void forceSendMsg(std::string_view msg)
    {
        while (!trySendMsg(msg))
            _mm_pause();
    }

    io::io_context ioContext;
    io::streambuf recvBuffer;
    io::ip::tcp::socket socket{ioContext};
    std::size_t nextSeqNum = 1u;
    static constexpr std::chrono::seconds HEARTBEAT_INTERVAL{25u};
    std::chrono::steady_clock::time_point heartbeatLastSent = std::chrono::steady_clock::now();

    bool isRunning = false;

    FIXMessageBuilder fixBuilder;

    std::chrono::steady_clock::time_point lastSent = std::chrono::steady_clock::now();
    std::chrono::seconds const interval{1u};
    std::uint64_t msgCountInterval = 0u;
};

} // namespace phoenix::data
