#pragma once

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace phoenix {

struct FIXCircularBuffer
{
    boost::asio::mutable_buffer getAsioBuffer();
    std::optional<std::string_view> getMsg(std::size_t bytesRead);

private:
    void advanceMoveOverflow(std::size_t newStart, std::size_t newEnd);

    static constexpr std::size_t BUFFER_CAPACITY{32768u};
    static constexpr std::size_t BUFFER_WRAP_BOUNDARY{8192u}; // assuming we never get more than this per socket read

    std::array<char, BUFFER_CAPACITY> buffer;
    std::size_t start = 0u;
    std::size_t end = 0u;
};

}