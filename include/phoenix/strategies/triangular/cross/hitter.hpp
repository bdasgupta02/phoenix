#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/graph/router_handler.hpp"
#include "phoenix/tags.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace phoenix::triangular {

template<typename NodeBase>
struct Hitter : NodeBase
{
    using Router = NodeBase::Router;
    using Config = NodeBase::Config;
    using Traits = NodeBase::Traits;
    using Price = NodeBase::Traits::PriceType;
    using Volume = NodeBase::Traits::VolumeType;
    using PriceValue = NodeBase::Traits::PriceType::ValueType;
    using Order = SingleOrder<Traits>;

    Hitter(Config const& config, RouterHandler<Router>& handler)
        : NodeBase(config, handler)
        , config{&config}
        , handler{&handler}
        , threshold{config.triggerThreshold}
        , qtyThreshold{config.qtyThreshold}
    {}

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::MDUpdate, FIXReader&& marketData, bool const update = true)
    {
        auto const& instrumentMap = config->instrumentMap;
        auto const& symbol = marketData.getString("55");
        auto const it = instrumentMap.find(symbol);
        PHOENIX_LOG_VERIFY(handler, (it != instrumentMap.end()), "Unknown instrument", symbol);

        ///////// UPDATE PRICES
        Price newBid;
        Price newAsk;
        std::size_t const numUpdates = marketData.getNumber<std::size_t>("268");
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            if (typeField == 0u)
                newBid.minOrZero(marketData.getDecimal<Price>("270", i));
            if (typeField == 1u)
                newAsk.minOrZero(marketData.getDecimal<Price>("270", i));
        }

        auto& instrumentPrices = bestPrices[it->second];
        if (newBid)
            instrumentPrices.bid = newBid;
        if (newAsk)
            instrumentPrices.ask = newAsk;

        if (!newBid && !newAsk)
        {
            PHOENIX_LOG_WARN(handler, "Invalid prices");
            return;
        }

        ///////// TRIGGER
        if (!update || fillMode)
            return;

        PHOENIX_LOG_VERIFY(handler, (instrumentPrices.bid < instrumentPrices.ask), "Overlapping prices");

        auto& btc = bestPrices[0];
        auto& eth = bestPrices[1];
        auto& cross = bestPrices[2];

        double const volume = config->volumeSize;
        double const contract = config->contractSize;

        // Buy BTC, Sell ETH, Buy ETH/BTC
        if (btc.ask * cross.ask < eth.bid)
        {
            PHOENIX_LOG_INFO(
                handler, "[OPP CASE 1] BTC", btc.ask.asDouble(), "* ETH/BTC", cross.ask.asDouble(), "< ETH", eth.bid.asDouble());

            auto const btcQty = btc.ask * contract;
            auto const ethQty = btcQty / eth.bid;
            auto const ethQtyLots = static_cast<double>(std::round(ethQty.asDouble() / contract));
            
            // clang-format off
            Order buyBtc{
                .symbol = config->instrumentList[0],
                .volume = 1.0,
                .side = 1,
                .isLimit = false
            };

            Order sellEth{
                .symbol = config->instrumentList[1],
                .volume = ethQtyLots,
                .side = 2,
                .isLimit = false
            };

            Order buyCross{
                .symbol = config->instrumentList[2],
                .volume = ethQtyLots,
                .side = 1,
                .isLimit = false
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, buyBtc, sellEth, buyCross))
            {
                sentOrders[0] = buyBtc;
                sentOrders[1] = sellEth;
                sentOrders[2] = buyCross;
                fillMode = true;
                filled = 0u;
            }
        }

        // Sell BTC, Buy ETH, Sell ETH/BTC
        if (eth.ask < (btc.bid * cross.bid))
        {
            PHOENIX_LOG_INFO(
                handler, "[OPP CASE 2] BTC", btc.bid.asDouble(), "* ETH/BTC", cross.bid.asDouble(), "> ETH", eth.ask.asDouble());

            auto const btcQty = btc.bid * contract;
            auto const ethQty = btcQty / eth.ask;
            auto const ethQtyLots = static_cast<double>(std::round(ethQty.asDouble() / contract));
            
            // clang-format off
            Order sellBtc{
                .symbol = config->instrumentList[0],
                .volume = 1.0,
                .side = 2,
                .isLimit = false
            };

            Order buyEth{
                .symbol = config->instrumentList[1],
                .volume = ethQtyLots,
                .side = 1,
                .isLimit = false
            };

            Order sellCross{
                .symbol = config->instrumentList[2],
                .volume = ethQtyLots,
                .side = 2,
                .isLimit = false
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, sellBtc, buyEth, sellCross))
            {
                sentOrders[0] = sellBtc;
                sentOrders[1] = buyEth;
                sentOrders[2] = sellCross;
                fillMode = true;
                filled = 0u;
            }
        }
    }

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::ExecutionReport, FIXReader&& report)
    {
        auto const& symbol = report.getString("55");
        auto status = report.getNumber<unsigned>("39");
        auto const& orderId = report.getString("11");
        auto const& clOrderId = report.getString("41");
        auto remaining = report.getDecimal<Volume>("151");
        auto justExecuted = report.getDecimal<Volume>("14");
        auto side = report.getNumber<unsigned>("54");
        auto price = report.getDecimal<Price>("44");
        PHOENIX_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        switch (status)
        {
        case 0: logOrder("[NEW ORDER]", orderId, side, price, remaining); break;

        case 1: logOrder("[PARTIAL FILL]", orderId, side, price, justExecuted); break;

        case 2:
        {
            unsigned const numFills = report.getNumber<unsigned>("1362");
            double avgFillPrice = 0.0;
            double totalQty = 0.0;
            for (unsigned i = 0u; i < numFills; ++i)
            {
                double const fillQty = report.getNumber<double>("1365", i);
                double const fillPrice = report.getNumber<double>("1364", i);
                totalQty += fillQty;
                avgFillPrice += (fillQty * fillPrice);
            }
            if (totalQty && avgFillPrice)
                avgFillPrice /= totalQty;

            logOrder("[FILL]", orderId, side, avgFillPrice, justExecuted);

            auto const it = config->instrumentMap.find(symbol);
            PHOENIX_LOG_VERIFY(handler, (it != config->instrumentMap.end()), "Symbol", symbol, "doesn't exist");
            auto& sentOrder = sentOrders[it->second];
            sentOrder.isFilled = true;
            sentOrder.price = avgFillPrice;

            if (++filled == 3u)
            {
                fillMode = false;
                filled = 0u;
                PHOENIX_LOG_INFO(handler, "All orders filled");
                updatePnl();
            }
        }
        break;

        case 4: logOrder("[CANCELLED]", orderId, side, price, remaining); break;

        case 8:
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
        }
        break;

        default: PHOENIX_LOG_WARN(handler, "Other status type", status); break;
        };
    }

    inline void handle(tag::Hitter::InitBalances) {}

private:
    [[gnu::hot, gnu::always_inline]]
    inline void updatePnl()
    {
        auto& eth = sentOrders[1];
        double ethPrice = eth.price.asDouble();
        double btcPrice = sentOrders[0].price.asDouble();
        double crossPrice = sentOrders[2].price.asDouble();

        double const contractSize = config->contractSize;
        double const multiplier = eth.volume.asDouble() * contractSize;

        if (sentOrders[0].side == 1)
            pnl += (ethPrice - (btcPrice * crossPrice)) * multiplier;
        else
            pnl += ((btcPrice * crossPrice) - ethPrice) * multiplier;

        PHOENIX_LOG_INFO(handler, "[PNL]", pnl, " in USD (estimate)");
    }

    [[gnu::hot, gnu::always_inline]]
    inline void logOrder(
        std::string_view type,
        std::string_view orderId,
        unsigned side,
        Price price,
        Volume volume,
        std::string_view rejectReason = "")
    {
        PHOENIX_LOG_INFO(
            handler,
            type,
            orderId,
            side == 1 ? "BUY" : "SELL",
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

    RouterHandler<Router>* const handler;
    Config const* const config;
    Price threshold;
    double qtyThreshold;

    std::array<InstrumentTopLevel, 3u> bestPrices;
    std::array<Order, 3u> sentOrders;

    bool fillMode = false;
    unsigned filled = 0u;

    double pnl = 0.0;
};

} // namespace phoenix::triangular
