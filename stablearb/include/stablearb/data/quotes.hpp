#pragma once

namespace stablearb {

template<typename Traits>
struct SingleQuote
{
    using PriceType = Traits::PriceType;
    using VolumeType = Traits::VolumeType;

    PriceType price;
    VolumeType volume;
    int side; // bid: 1, ask: 2

    bool takeProfit = false;
};

} // namespace stablearb
