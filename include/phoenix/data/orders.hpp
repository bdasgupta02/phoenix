#pragma once

namespace phoenix {

template<typename Traits>
struct SingleOrder
{
    using PriceType = Traits::PriceType;
    using VolumeType = Traits::VolumeType;

    std::string_view symbol;
    PriceType price;
    VolumeType volume;
    unsigned int side; // bid: 1, ask: 2

    bool isLimit = true;
    bool takeProfit = false;
    bool isFilled = false;
    bool isFOK = false;
    
    std::string orderId;
};

} // namespace phoenix
