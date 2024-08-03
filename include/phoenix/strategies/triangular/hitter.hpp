#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/tags.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace phoenix::triangular {

template<typename NodeBase>
struct Hitter : NodeBase
{
    using NodeBase::NodeBase;
    using Price = NodeBase::Traits::PriceType;
    using Volume = NodeBase::Traits::VolumeType;
    using PriceValue = NodeBase::Traits::PriceType::ValueType;

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::MDUpdate, FIXReader&& marketData, bool update = true)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();
        auto const& instrumentMap = config->instrumentMap;

        auto const& symbol = marketData.getString("55");
        auto it = instrumentMap.find(symbol);
        PHOENIX_LOG_VERIFY(handler, (it != instrumentMap.end()), "Unknown instrument", symbol);
        std::size_t instrumentIdx = it->second;

        ///////// UPDATE PRICES

        std::int64_t bidIdx = -1u;
        std::int64_t askIdx = -1u;

        Price newBid;
        Price newAsk;

        std::size_t const numUpdates = marketData.getFieldSize("269");
        auto& instrumentPrices = bestPrices[instrumentIdx];
        for (std::int64_t i = 0; i < numUpdates; ++i)
        {
            auto typeField = marketData.getNumber<unsigned int>("269", i);

            if (typeField == 0)
                bidIdx = i;

            if (typeField == 1)
                askIdx = i;
        }

        if (bidIdx > -1)
        {
            newBid = marketData.getDecimal<Price>("270", bidIdx);
            if (PriceValue{} != newBid)
                instrumentPrices.bid = newBid;
        }

        if (askIdx > -1)
        {
            newAsk = marketData.getDecimal<Price>("270", askIdx);
            if (PriceValue{} != newAsk)
                instrumentPrices.ask = newAsk;
        }

        /*PHOENIX_LOG_INFO(*/
        /*    handler,*/
        /*    "[MARKET DATA]",*/
        /*    symbol,*/
        /*    instrumentPrices.bestBid.str(),*/
        /*    instrumentPrices.bestAsk.str());*/

        if (!update)
            return;

        ///////// TRIGGER
        // 2 cases:
        // - buy ETH/USDC sell STETH/ETH sell STETH/USDC
        //    : USDC > ETH > STETH > USDC
        //    : if ETH/USDC bid > (STETH/USDC ask * STETH/ETH bid)
        // - buy STETH/USDC buy STETH/ETH sell ETH/USDC : USDC > STETH > ETH > USDC :
        //    : USDC > STETH > ETH > USDC
        //    : if STETH/USDC bid > (ETH/USDC ask / STETH/ETH ask)

        auto& eth = bestPrices[0];
        auto& steth = bestPrices[1];
        auto& bridge = bestPrices[2];

        auto stethConv = (steth.ask * bridge.bid);
        if (eth.bid > stethConv)
        {
            PHOENIX_LOG_INFO(handler, "[OPPORTUNITY ETH]", eth.bid.str(), stethConv.str(), (eth.bid - stethConv).str());
        }

        auto ethConv = (eth.ask / bridge.ask);
        if (steth.bid > ethConv)
        {
            PHOENIX_LOG_INFO(
                handler, "[OPPORTUNITY STETH]", steth.bid.str(), ethConv.str(), (steth.bid - ethConv).str());
        }
    }

    void handle(tag::Hitter::ExecutionReport, FIXReader&& report) {}

private:
    struct InstrumentTopLevel
    {
        Price bid{PriceValue{}};
        Price ask{std::numeric_limits<PriceValue>::max()};
    };

    std::array<InstrumentTopLevel, 3u> bestPrices;
};

} // namespace phoenix::triangular
