#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/common/profiler.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/tools/fix_circular_buffer.hpp"
#include "phoenix/tags.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <concepts>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

#include <immintrin.h>
#include <linux/socket.h>

namespace {
namespace io = ::boost::asio;
} // namespace

namespace phoenix {

template<typename NodeBase>
struct TCPSocket : NodeBase
{ 
    using Traits = NodeBase::Traits;
    using NodeBase::NodeBase;

    inline void handle(tag::TCPSocket::Stop, std::string_view logoutMsg)
    {
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(handler, "Stopping stream");

        handler->invoke(tag::TCPSocket::ForceSend{}, logoutMsg);
        boost::system::error_code error;
        socket.close(error);

        if (error)
            PHOENIX_LOG_ERROR(handler, "Error closing socket", error.message());
    }

    inline void handle(tag::TCPSocket::Connect, std::string const& host, std::string const& portStr, bool isColo = true)
    {
        try
        {
            if (isColo)
            {
                int port = std::atoi(portStr.c_str());
                io::ip::tcp::endpoint endpoint(io::ip::address::from_string(host), port);
                socket.connect(endpoint);
            }
            else
            {
                io::ip::tcp::resolver resolver{ioContext};
                auto endpoints = resolver.resolve(host, portStr);
                io::connect(socket, endpoints);
            }
            
            socket.set_option(io::ip::tcp::no_delay(true));
            socket.set_option(boost::asio::socket_base::receive_buffer_size(256 * 1024));
            socket.set_option(boost::asio::socket_base::send_buffer_size(256 * 1024));

            setsockopt(socket.native_handle(), SOL_SOCKET, SO_PRIORITY, &SOCKET_PRIORITY, sizeof(SOCKET_PRIORITY));
            setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_QUICKACK, &SOCKET_ENABLE_FLAG, sizeof(SOCKET_ENABLE_FLAG));
            setsockopt(socket.native_handle(), SOL_SOCKET, SO_BUSY_POLL, &SOCKET_ENABLE_FLAG, sizeof(SOCKET_ENABLE_FLAG));
 
            PHOENIX_LOG_INFO(this->getHandler(), "Connected successfully");
        }
        catch (std::exception const& e)
        {
            PHOENIX_LOG_FATAL(this->getHandler(), "Connection error", e.what());
        }
    }

    template<typename... Messages>
    inline void handle(tag::TCPSocket::ForceSend, Messages&&... messages)
    {
        while (!checkThrottle(sizeof...(Messages)))
            _mm_pause();

        (sendUnthrottled(messages), ...);
    }

    template<typename... Messages>
    inline bool handle(tag::TCPSocket::Send, Messages&&... messages)
    {
        if (!checkThrottle(sizeof...(Messages)))
            return false;

        (sendUnthrottled(messages), ...);
        return true;
    }

    inline bool handle(tag::TCPSocket::CheckThrottle, std::size_t req)
    {
        return checkThrottle(req);
    }

    inline void handle(tag::TCPSocket::SendUnthrottled, std::string_view msg)
    {
        sendUnthrottled(msg);
    }

    inline std::string_view handle(tag::TCPSocket::ForceReceive)
    {
        auto msg = handle(tag::TCPSocket::Receive{});
        while (!msg)
        {
            _mm_pause();
            msg = handle(tag::TCPSocket::Receive{});
        }

        return *msg;
    };

    inline std::optional<std::string_view> handle(tag::TCPSocket::Receive)
    {
        auto leftoverMsg = circularBuffer.getMsg(0u);
        if (leftoverMsg)
            return leftoverMsg;

        if (!socket.available())
            return {};

        auto* handler = this->getHandler();
        boost::system::error_code error;
        auto const bytesRead = socket.read_some(circularBuffer.getAsioBuffer(), error);
        PHOENIX_LOG_VERIFY(handler, (!error), "Error while receiving message", error.message());
        return circularBuffer.getMsg(bytesRead);
    };

private:
    inline bool checkThrottle(std::size_t numMessages)
    {
        auto nextAllowed = lastSent + THROTTLE_INTERVAL;
        auto now = std::chrono::steady_clock::now();

        if (msgCountInterval <= MESSAGES_IN_INTERVAL - numMessages)
        {
            msgCountInterval += numMessages;
            return true;
        }
        else if (now >= nextAllowed)
        {
            lastSent = now;
            msgCountInterval = numMessages;
            return true;
        }
        else
            return false;
    }

    inline void sendUnthrottled(std::string_view msg)
    {
        boost::system::error_code error;
        io::write(socket, io::buffer(msg), error);
        PHOENIX_LOG_VERIFY(this->getHandler(), (!error), "Error while sending message", msg, error.message());
    }

    // setup
    static constexpr int SOCKET_PRIORITY{6};
    static constexpr int SOCKET_ENABLE_FLAG{1};

    // socket
    io::io_context ioContext;
    io::ip::tcp::socket socket{ioContext};
    FIXCircularBuffer circularBuffer;
    
    // throttling
    static constexpr std::chrono::seconds THROTTLE_INTERVAL{1u};
    static constexpr std::size_t MESSAGES_IN_INTERVAL{5u};
    std::chrono::steady_clock::time_point lastSent = std::chrono::steady_clock::now();
    std::uint64_t msgCountInterval = 0u;
};

}
