#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/tags.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace phoenix::triangular {

template<typename NodeBase>
struct Hitter : NodeBase
{
    using NodeBase::NodeBase;
    using Traits = NodeBase::Traits;
    using Price = NodeBase::Traits::PriceType;
    using Volume = NodeBase::Traits::VolumeType;
    using PriceValue = NodeBase::Traits::PriceType::ValueType;
    using Order = SingleOrder<Traits>;

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
            if (Price{} != newBid)
                instrumentPrices.bid = newBid;
        }

        if (askIdx > -1)
        {
            newAsk = marketData.getDecimal<Price>("270", askIdx);
            if (Price{} != newAsk)
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
        // - buy ETH/USDC ask, buy STETH/ETH ask sell STETH/USDC bid
        //    : USDC > ETH > STETH > USDC
        // - buy STETH/USDC ask, sell STETH/ETH bid, sell ETH/USDC, bid
        //    : USDC > STETH > ETH > USDC

        // the variables below are:
        // - eth: ETH/USDC
        // - steth: STETH/USDC
        // - bridge: STETH/ETH

        auto& eth = bestPrices[0];
        auto& steth = bestPrices[1];
        auto& bridge = bestPrices[2];

        if (eth.ask * bridge.ask < steth.bid && eth.ask < steth.bid)
        {
            handler->invoke(
                tag::Stream::TakeTriangular{},
                false,
                Order{.price = eth.ask, .volume = Volume{1.0}, .side = 1}, //
                Order{.price = bridge.ask, .volume = Volume{1.0}, .side = 1}, //
                Order{.price = steth.bid, .volume = Volume{1.0}, .side = 2});
        }

        if (steth.ask < eth.bid * bridge.bid && steth.ask < eth.bid)
        {
            handler->invoke(
                tag::Stream::TakeTriangular{},
                true,
                Order{.price = steth.ask, .volume = Volume{1.0}, .side = 1}, //
                Order{.price = bridge.bid, .volume = Volume{1.0}, .side = 2}, //
                Order{.price = eth.bid, .volume = Volume{1.0}, .side = 2});
        }
    }

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::ExecutionReport, FIXReader&& report)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto symbol = report.getStringView("55");
        auto status = report.getNumber<unsigned int>("39");
        auto const& orderId = report.getString("11");
        auto const& clOrderId = report.getString("41");
        auto remaining = report.getDecimal<Volume>("151");
        auto justExecuted = report.getDecimal<Volume>("14");
        auto side = report.getNumber<unsigned int>("54");
        auto price = report.getDecimal<Price>("44");
        PHOENIX_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        // new order
        if (status == 0)
            logOrder("[NEW ORDER]", orderId, clOrderId, side, price, remaining);

        // partial/total fill
        if (status == 1 || status == 2)
            logOrder("[FILL]", orderId, clOrderId, side, price, justExecuted);

        // cancelled
        if (status == 4)
            logOrder("[CANCELLED]", orderId, clOrderId, side, price, remaining);

        // rejected
        if (status == 8)
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, clOrderId, side, price, remaining, reason);
        }
    }

private:
    [[gnu::hot, gnu::always_inline]]
    inline void logOrder(
        std::string_view type,
        std::string_view orderId,
        std::string_view clOrderId,
        unsigned int side,
        Price price,
        Volume volume,
        std::string_view rejectReason = "")
    {
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(
            handler,
            type,
            orderId,
            clOrderId,
            side == 1 ? "BID" : "ASK",
            volume.asDouble(),
            '@',
            price.asDouble(),
            rejectReason.empty() ? "" : "with reason",
            rejectReason);
    }

    struct InstrumentTopLevel
    {
        Price bid{PriceValue{}};
        Price ask{std::numeric_limits<PriceValue>::max()};
    };

    std::array<InstrumentTopLevel, 3u> bestPrices;
};

} // namespace phoenix::triangular
