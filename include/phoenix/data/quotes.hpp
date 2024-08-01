#pragma once

namespace phoenix {

template<typename Traits>
struct SingleQuote
{
    using PriceType = Traits::PriceType;
    using VolumeType = Traits::VolumeType;

    PriceType price;
    VolumeType volume;
    unsigned int side; // bid: 1, ask: 2

    bool takeProfit = false;
};

} // namespace phoenix
