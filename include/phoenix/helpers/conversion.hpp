#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace phoenix {

inline std::string_view uint64ToString(std::uint64_t value)
{
    static constexpr char digits[] = "0123456789";
    static std::array<char, 20> buffer;

    char* p = buffer.data() + 20;

    do
    {
        *--p = digits[value % 10];
        value /= 10;
    }
    while (value);

    return std::string_view(p, buffer.data() + 20 - p);
}

} // namespace phoenix
