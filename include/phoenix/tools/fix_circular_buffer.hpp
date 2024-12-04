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

    static constexpr std::size_t BUFFER_CAPACITY{16384u};
    static constexpr std::size_t BUFFER_WRAP_BOUNDARY{4096u}; // assuming we never get more than this per socket read
    static constexpr std::size_t FIX_CHECKSUM_LENGTH{7u}; // ['1', '0', '=', '1', '2', '3', '\x01'] without preceding SOH character
    static constexpr std::size_t FIX_EXPECTED_MIN_LENGTH{17u}; // 8=FIX4.49(SOH), 10=123(SOH)

    std::array<char, BUFFER_CAPACITY> buffer;
    std::size_t start = 0u;
    std::size_t end = 0u;
};

}
