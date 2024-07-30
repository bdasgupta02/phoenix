#pragma once

#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <exception>
#include <string_view>

namespace stablearb {

namespace detail {
template<std::uint8_t Precision>
consteval std::uint64_t getMultiplier()
{
    std::uint64_t result = 1;
    for (std::uint8_t i = 0; i < Precision; ++i)
        result *= 10;

    return result;
}
} // namespace detail

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

    Decimal(Decimal&&) = default;
    Decimal& operator=(Decimal&&) = default;

    Decimal(Decimal&) = default;
    Decimal& operator=(Decimal&) = default;

    std::uint64_t data() const { return value; }

    template<std::floating_point T>
    T as() const
    {
        return static_cast<T>(value) / multiplier;
    }

    std::string str() const
    {
        std::uint64_t left = (value / multiplier) * (multiplier * 10);
        std::uint64_t right = (value % multiplier) + multiplier;

        std::string result;

        if (left == 0)
        {
            result = std::to_string(left + right + (multiplier * 10));
            std::uint64_t dotIdx = result.size() - Precision - 1;
            result[0] = '0';
            result[dotIdx] = '.';
        }
        else
        {
            result = std::to_string(left + right);
            std::uint64_t dotIdx = result.size() - Precision - 1;
            result[dotIdx] = '.';
        }

        return std::move(result);
    }

    Decimal operator+(Decimal const& other) const { return {value + other.value}; }
    Decimal operator-(Decimal const& other) const { return {value - other.value}; }

    auto operator<=>(double other) { return as<double>() <=> other; }
    auto operator<=>(Decimal const&) const = default;

    void modify(auto&& func) { value = func(value); }

private:
    std::uint64_t value = 0ULL;
    static constexpr std::uint64_t multiplier = detail::getMultiplier<Precision>();
};

} // namespace stablearb
