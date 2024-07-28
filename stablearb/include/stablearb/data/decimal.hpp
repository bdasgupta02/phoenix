#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <exception>
#include <string_view>

namespace stablearb {

template<std::uint8_t Precision>
class Decimal
{
public:
    Decimal() = default;

    Decimal(double value)
        : value(std::round(value * multiplier))
    {}

    Decimal(std::uint64_t value)
        : value(value)
    {}

    std::uint64_t data() const { return value; }

    template<std::floating_point T>
    T as() const
    {
        return static_cast<T>(value) / multiplier;
    }

    std::string_view str() const
    {
        char buffer[21];
        char* ptr = buffer;

        std::uint64_t const left = value / multiplier;
        std::uint64_t const right = value % multiplier;
        std::uint64_t const combined = (left * multiplier * 10) + right;

        auto const result = std::to_chars(ptr, ptr + sizeof(buffer), combined);
        std::size_t const length = result.ptr - ptr;
        char* const dot = ptr + (length - Precision);
        *dot = '.';

        return {ptr, length};
    }

    Decimal operator+(Decimal const& other) const { return {value + other.value}; }

    Decimal operator-(Decimal const& other) const { return {value - other.value}; }

    void modify(auto&& func) { value = func(value); }

private:
    std::uint64_t value = 0ULL;
    static constexpr std::uint64_t multiplier = std::pow(10, Precision);
};

} // namespace stablearb
