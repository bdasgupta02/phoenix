#pragma once

#include "boost/unordered/unordered_flat_map.hpp"

#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {
template<typename T>
concept numerical = (std::integral<T> || std::floating_point<T>) && !std::same_as<T, char> && !std::same_as<T, bool>;
}

namespace stablearb {

// Assumes synchronous connectivity and tries to reduce copies
struct FIXBuilder
{
    constexpr FIXBuilder()
        : buffer(8192u)
    {}

    constexpr FIXBuilder(FIXBuilder&&) = default;
    constexpr FIXBuilder& operator=(FIXBuilder&&) = default;

    inline void clear()
    {
        buffer.clear();
        size = 0;
    }

    template<typename Traits>
    inline void append(std::string_view tag, Traits::PriceType& value)
    {
        append(tag, value.str());
    }

    inline void append(std::string_view tag, numerical auto value)
    {
        static char convBuffer[512];
        static char* ptr;
        ptr = convBuffer;

        auto const result = std::to_chars(ptr, ptr + sizeof(convBuffer), value);
        std::string_view str(ptr, result.ptr - ptr);

        append(tag, str);
    }

    inline void append(std::string_view tag, bool value) { append(tag, value ? 'Y' : 'N'); }

    inline void append(std::string_view tag, std::string_view value)
    {
        append(tag);
        append('=');
        append(value);
        append('|');
    }

    inline std::string_view serialize()
    {
        std::string_view msg = {buffer.data(), size};
        std::string checksum = calculateChecksum(msg);
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

    std::string calculateChecksum(std::string_view message)
    {
        int checksum = 0;
        for (char c : message)
            checksum += static_cast<unsigned char>(c);
        checksum %= 256;

        char checksumStr[4];
        std::snprintf(checksumStr, sizeof(checksumStr), "%03d", checksum);
        return {checksumStr};
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

    std::string_view getString(std::string const& tag) { return fields[tag]; }

    template<numerical T>
    T getNumber(std::string const& tag)
    {
        auto val = fields[tag];
        T result;
        std::from_chars(val.data(), val.data() + val.size(), result);
        return result;
    }

    template<typename Traits>
    Traits::PriceType getPrice(std::string const& tag)
    {
        double value = getNumber<double>(tag);
        return {value};
    }

    bool getBool(std::string const& tag)
    {
        auto val = getString(tag);
        if (val == "Y" || val == "y" || val == "1" || val == "true" || val == "TRUE" || val == "T" || val == "t")
            return true;

        return false;
    }

private:
    boost::unordered_flat_map<std::string, std::string_view> fields;
};

namespace fix_msg {

FIXBuilder login(std::string_view username, std::string_view password)
{
    FIXBuilder builder;
    builder.append("8", "FIX.4.4");
    builder.append("35", "A");
    builder.append("49", "senderCompID");
    builder.append("56", "targetCompID");
    builder.append("34", 1);

    builder.append("553", username);
    builder.append("554", password);

    auto now = std::chrono::system_clock::now();
    auto nowC = std::chrono::system_clock::to_time_t(now);
    char timeStr[20];
    std::strftime(timeStr, sizeof(timeStr), "%Y%m%d-%H:%M:%S", std::gmtime(&nowC));
    builder.append("52", timeStr); // SendingTime

    return std::move(builder);
}

} // namespace fix_msg

} // namespace stablearb
