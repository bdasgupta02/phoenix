#pragma once

namespace phoenix {

template<typename Price, std::size_t WindowSize>
struct RollingAverage
{
    void add(Price value)
    {
        if (filled)
            sum -= values[index];

        values[index] = value;
        sum += value;
        
        index = (index + 1u) % WindowSize;
        if (index == 0u)
            filled = true;
    }

    Price get() const
    {
        return sum / (filled ? Price{static_cast<double>(WindowSize)} : Price{static_cast<double>(index)});
    }

    std::array<Price, WindowSize> values{};
    std::size_t index = 0u;
    Price sum = 0.0;
    bool filled = false;
};

}
