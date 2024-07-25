#pragma once

#include <cmath>
#include <cstdint>

namespace stablearb {

template<std::uint8_t Precision>
class Price
{
public:
    Price(double price)
        : value(std::round(price * multiplier))
    {}

    Price(std::uint64_t value)
        : value(value)
    {}

    std::uint64_t data() const { return value; }

    template<typename T>
    T as() const
    {
        return static_cast<T>(value) / multiplier;
    }

    Price operator+(Price const& other) const { return {value + other.value}; }

    Price operator-(Price const& other) const { return {value - other.value}; }

private:
    std::uint64_t value = 0ULL;
    static constexpr std::uint64_t multiplier = std::pow(10, Precision);
};

} // namespace stablearb
