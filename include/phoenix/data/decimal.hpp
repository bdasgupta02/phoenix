#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace phoenix {

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

// unsigned
template<std::uint8_t Precision>
class Decimal
{
public:
    using ValueType = std::uint64_t;

    constexpr Decimal() = default;

    constexpr Decimal(std::floating_point auto value)
        : value(std::round(value * MULTIPLIER))
    {}

    constexpr Decimal(std::integral auto value)
        : value(value)
    {}

    constexpr Decimal(std::string_view str)
    {
        bool seenDecimal = false;
        std::uint64_t integerPart = 0;
        std::uint64_t fractionalPart = 0;
        std::uint8_t fractionalDigits = 0;

        auto start = str.begin();
        auto end = str.end();

        for (auto it = start; it < end; ++it)
        {
            char ch = *it;

            if (ch == '.')
                seenDecimal = true;
            else if (ch >= '0' && ch <= '9')
            {
                if (seenDecimal)
                {
                    if (fractionalDigits < Precision)
                    {
                        fractionalPart = fractionalPart * 10 + (ch - '0');
                        ++fractionalDigits;
                    }
                }
                else
                    integerPart = integerPart * 10 + (ch - '0');
            }
            else
                this->error = true;
        }

        if (fractionalDigits < Precision)
            fractionalPart *= static_cast<std::uint64_t>(std::pow(10, Precision - fractionalDigits));

        value = (integerPart * MULTIPLIER) + fractionalPart;
    }

    // must be null terminated
    constexpr Decimal(char const* ptr)
    {
        std::size_t nullIdx = 0u;
        while (*ptr != '\0')
            ++ptr;

        *this = Decimal{std::string_view{ptr, nullIdx}};
    }

    constexpr Decimal(Decimal&&) = default;
    constexpr Decimal& operator=(Decimal&&) = default;

    constexpr Decimal(Decimal const&) = default;
    constexpr Decimal& operator=(Decimal const&) = default;

    constexpr std::uint64_t data() const { return value; }

    template<std::floating_point T>
    T as() const
    {
        return static_cast<T>(value) / MULTIPLIER;
    }

    constexpr double asDouble() const { return static_cast<double>(value) / MULTIPLIER; }
    constexpr std::uint64_t getValue() const { return value; }

    std::string str() const
    {
        std::uint64_t integerPart = value / MULTIPLIER;
        std::uint64_t fractionalPart = value % MULTIPLIER;

        std::string fractionalPartStr = std::to_string(fractionalPart);
        if (fractionalPartStr.length() < Precision)
        {
            fractionalPartStr.insert(fractionalPartStr.begin(), Precision - fractionalPartStr.length(), '0');
        }

        fractionalPartStr.erase(fractionalPartStr.find_last_not_of('0') + 1, std::string::npos);

        if (fractionalPartStr.empty())
            return std::to_string(integerPart);

        return std::to_string(integerPart) + '.' + fractionalPartStr;
    }

    constexpr void minOrZero(Decimal const& other)
    {
        if (other && (other.value < value || !value))
            value = other.value;
    }

    constexpr Decimal operator+(Decimal other) const { return {value + other.value}; }
    constexpr Decimal operator+(double other) const { return {asDouble() + other}; }
    
    constexpr Decimal operator-(Decimal other) const { return {value - other.value}; }
    constexpr Decimal operator-(double other) const { return {asDouble() - other}; }

    constexpr void operator+=(Decimal other) { value += other.value; }
    constexpr void operator+=(double other) { value += other * MULTIPLIER; }

    constexpr void operator-=(Decimal other) { value -= other.value; }
    constexpr void operator-=(double other) { value -= other * MULTIPLIER; }

    constexpr Decimal operator*(Decimal const& other) const { return {(value * other.value) / MULTIPLIER}; }
    friend constexpr Decimal operator*(Decimal dec, double raw) { return {dec.asDouble() * raw}; }
    friend constexpr Decimal operator*(double raw, Decimal dec) { return {raw * dec.asDouble()}; }

    constexpr Decimal operator/(Decimal const& other) const { return {asDouble() / other.asDouble()}; }
    friend constexpr Decimal operator/(Decimal dec, double raw) { return {dec.asDouble() / raw}; }
    friend constexpr Decimal operator/(double raw, Decimal dec) { return {raw / dec.asDouble()}; }

    constexpr auto operator<=>(std::floating_point auto other) const { return asDouble() <=> other; }
    constexpr auto operator<=>(std::integral auto other) const { return value <=> other; }
    constexpr auto operator<=>(Decimal const& other) const { return value <=> other.value; };

    constexpr auto operator==(Decimal const& other) const { return value == other.value; };
    constexpr auto operator!=(Decimal const& other) const { return value != other.value; };

    explicit constexpr operator bool() const { return value != 0ULL; }

    constexpr void modify(auto&& func) { value = func(value); }

    bool error = false;

private:
    std::uint64_t value = 0ULL;
    static constexpr std::uint64_t MULTIPLIER = Precision == 0u ? 1 : detail::getMultiplier<Precision>();
};

} // namespace phoenix
