#pragma once

#include "stablearb/helpers/conversion.hpp"

#include "boost/unordered/unordered_flat_map.hpp"

#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// Existing C++ implementations of FIX protocol were boring,
// so this is one that reduces copying and allocations, and assumes single-threaded use

// TODO: equivalence between reader and writer, or raw string and writer
// or tbh just check message type field in reader

namespace stablearb {

namespace concepts {
template<typename T>
concept Numerical = (std::integral<T> || std::floating_point<T>) && !std::same_as<T, char> && !std::same_as<T, bool>;
}

static constexpr char FIX_FIELD_DELIMITER = '\x01';
static constexpr char FIX_MSG_DELIMITER[] = "\x018=";
static constexpr char FIX_PROTOCOL[] = "FIX.4.4";
static constexpr std::size_t FIX_PROTOCOL_MSG_LENGTH = sizeof(FIX_PROTOCOL) - 4; // 1 for \0; 3 for tag, =, \x01

// Assumes synchronous connectivity and tries to reduce copies
struct FIXBuilder
{
    FIXBuilder();
    FIXBuilder(std::size_t seqNum, char msgType);

    FIXBuilder(FIXBuilder&&) = default;
    FIXBuilder& operator=(FIXBuilder&&) = default;
    FIXBuilder(FIXBuilder const&) = default;
    FIXBuilder& operator=(FIXBuilder const&) = default;

    inline void clear()
    {
        buffer.clear();
        size = 0;
    }

    inline void reset(std::size_t seqNum, char msgType)
    {
        clear();
        appendHeader(seqNum, msgType);
    }

    template<typename... Args>
    inline void appendAll(Args&&... args)
    {
        (append(std::forward<Args&&>(args)), ...);
    }

    template<typename Traits>
    inline void append(std::string_view tag, Traits::PriceType& value)
    {
        append(tag, value.str());
    }

    inline void append(std::string_view tag, concepts::Numerical auto value)
    {
        static char convBuffer[512];
        static char* ptr;
        ptr = convBuffer;

        auto const result = std::to_chars(ptr, ptr + sizeof(convBuffer), value);
        std::string_view str(ptr, result.ptr - ptr);

        append(tag, str);
    }

    inline void append(std::string_view tag, bool value) { append(tag, value ? 'Y' : 'N'); }

    inline void append(std::string_view tag, char value) { appendAll(tag, '=', value, FIX_FIELD_DELIMITER); }

    inline void append(std::string_view tag, char const* value) { append(tag, std::string_view(value)); }

    inline void append(std::string_view tag, std::string_view value)
    {
        appendAll(tag, '=', value, FIX_FIELD_DELIMITER);
    }

    inline void appendHeader(std::size_t seqNum, char msgType)
    {
        append("8", FIX_PROTOCOL);
        append("35", msgType);
        append("49", 'z');
        append("56", "DERIBITSERVER");
        append("34", 1);
    }

    inline std::string_view serialize()
    {
        std::size_t lengthWithoutProtocol = size - FIX_PROTOCOL_MSG_LENGTH;
        append("9", lengthWithoutProtocol);

        std::string_view checksum = calculateChecksum();
        append("10", checksum);

        return {buffer.data(), size};
    }

private:
    inline void append(std::string_view str)
    {
        buffer.insert(buffer.end(), str.begin(), str.end());
        size += str.end() - str.begin();
    }

    inline void append(char c)
    {
        buffer.push_back(c);
        ++size;
    }

    std::string_view calculateChecksum()
    {
        unsigned int checksum = 0;
        for (std::size_t i = 0u; i < size; ++i)
            checksum += static_cast<unsigned char>(buffer[i]);

        checksum %= 256;

        static char checksumStr[4];
        std::snprintf(checksumStr, sizeof(checksumStr), "%03d", checksum);
        return {checksumStr, 3};
    }

    std::vector<char> buffer;
    std::size_t size = 0u;
};

// Assumes synchronous connectivity and tries to reduce copies
struct FIXReader
{
    FIXReader(std::string_view data);

    FIXReader(FIXReader&&) = default;
    FIXReader& operator=(FIXReader&&) = default;

    inline std::string_view getString(std::string const& tag) { return fields[tag]; }

    template<concepts::Numerical T>
    inline T getNumber(std::string const& tag)
    {
        auto val = fields[tag];
        T result;
        std::from_chars(val.data(), val.data() + val.size(), result);
        return result;
    }

    template<typename Traits>
    inline Traits::PriceType getPrice(std::string const& tag)
    {
        double value = getNumber<double>(tag);
        return {value};
    }

    inline bool getBool(std::string const& tag)
    {
        auto val = getString(tag);
        return val == "Y" || val == "y";
    }

private:
    boost::unordered_flat_map<std::string, std::string_view> fields;
};

namespace fix_msg {

inline std::string_view
    login(std::size_t seqNum, std::string_view username, std::string_view password, std::string_view nonce)
{
    static FIXBuilder builder;
    builder.reset(seqNum, 'A');

    auto epoch = std::chrono::steady_clock::now().time_since_epoch();
    std::uint64_t timeEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
    std::string_view timeStr = uint64ToString(timeEpoch);

    builder.appendAll("96", '=', timeStr, '.', nonce, FIX_FIELD_DELIMITER);
    builder.append("108", 5);
    builder.append("553", username);
    builder.append("554", password);
    builder.append("9001", true);

    return builder.serialize();
}

inline std::string_view logout(std::size_t seqNum)
{
    static FIXBuilder builder;
    builder.reset(seqNum, '5');
    return builder.serialize();
}

} // namespace fix_msg

} // namespace stablearb
