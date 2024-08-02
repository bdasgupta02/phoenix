#pragma once

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

template<std::uint8_t Precision>
class Decimal
{
public:
    using ValueType = std::uint64_t;

    constexpr Decimal() = default;

    constexpr Decimal(double value)
        : value(std::round(value * multiplier))
    {}

    constexpr Decimal(std::uint64_t value)
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

        value = (integerPart * multiplier) + fractionalPart;
    }

    // must be null terminated
    constexpr Decimal(char const* ptr)
    {
        std::size_t nullIdx = 0u;
        while (*ptr != '\0')
            ++ptr;

        *this = Decimal{std::string_view{ptr, nullIdx}};
    }

    Decimal(Decimal&&) = default;
    Decimal& operator=(Decimal&&) = default;

    Decimal(Decimal const&) = default;
    Decimal& operator=(Decimal const&) = default;

    std::uint64_t data() const { return value; }

    template<std::floating_point T>
    T as() const
    {
        return static_cast<T>(value) / multiplier;
    }

    std::uint64_t getValue() const { return value; }

    std::string str() const
    {
        std::uint64_t integerPart = value / multiplier;
        std::uint64_t fractionalPart = value % multiplier;

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

    Decimal operator+(Decimal const& other) const { return {value + other.value}; }
    Decimal operator-(Decimal const& other) const { return {value - other.value}; }

    auto operator<=>(double other) { return as<double>() <=> other; }
    auto operator<=>(std::integral auto other) { return value <=> other; }
    auto operator<=>(Decimal const&) const = default;

    void modify(auto&& func) { value = func(value); }

    bool error = false;

private:
    std::uint64_t value = 0ULL;
    static constexpr std::uint64_t multiplier = Precision == 0u ? 1 : detail::getMultiplier<Precision>();
};

} // namespace phoenix
