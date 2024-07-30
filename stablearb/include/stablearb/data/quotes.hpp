#pragma once

namespace stablearb {

template<typename Traits>
struct SingleQuote
{
    using PriceType = Traits::PriceType;
    using VolumeType = Traits::VolumeType;

    PriceType price;
    VolumeType volume;
    int side;
};

struct QuoteState
{};

} // namespace stablearb
