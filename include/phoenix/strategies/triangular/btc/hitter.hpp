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
        Volume newBidQty;
        Volume newAskQty;
        std::size_t const numUpdates = marketData.getNumber<std::size_t>("268");
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            if (typeField == 0u)
            {
                newBid.minOrZero(marketData.getDecimal<Price>("270", i));
                newBidQty.minOrZero(marketData.getDecimal<Volume>("271", i));
            }
            if (typeField == 1u)
            {
                newAsk.minOrZero(marketData.getDecimal<Price>("270", i));
                newAskQty.minOrZero(marketData.getDecimal<Volume>("271", i));
            }
        }

        if (!newBid || !newAsk)
        {
            PHOENIX_LOG_WARN(handler, "Invalid prices");
            return;
        }

        auto& instrumentPrices = bestPrices[it->second];
        instrumentPrices.bid = newBid;
        instrumentPrices.bidQty = newBidQty;
        instrumentPrices.ask = newAsk;
        instrumentPrices.askQty = newAskQty;

        ///////// TRIGGER
        if (!update)
            return;

        if (fillMode)
        {
        }

        auto& btct = bestPrices[0];
        auto& btcc = bestPrices[1];
        auto& usdc = bestPrices[2];

        double const volume = config->volumeSize;
        double const contract = config->contractSize;

        // Buy BTC/T, Sell BTC/C, Sell USDC for USDT
        if (btcc.bid * usdc.bid > btct.ask)
        {
            // clang-format off
            Order buyBtct{
                .symbol = config->instrumentList[0],
                .price = btct.ask,
                .volume = volume,
                .side = 1,
            };

            Order sellBtcc{
                .symbol = config->instrumentList[1],
                .price = btcc.bid,
                .volume = volume,
                .side = 2,
            };

            Order sellUsdc{
                .symbol = config->instrumentList[2],
                .price = usdc.bid,
                .volume = volume,
                .side = 2,
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, buyBtct, sellBtcc, sellUsdc))
            {
                sentOrders[0] = buyBtct;
                sentOrders[1] = sellBtcc;
                sentOrders[2] = sellUsdc;
                fillMode = true;
                filled = 0u;
                PHOENIX_LOG_INFO(handler, "Taking case 1");
            }

            PHOENIX_LOG_INFO(handler, "[OPP CASE 1]", btcc.ask.asDouble(), '*', usdc.bid.asDouble(), '>', btct.bid.asDouble());
        }

        // Buy BTC/C, Sell BTC/T, Buy USDC for USDT
        if (btct.bid > btcc.ask * usdc.ask)
        {
            // clang-format off
            Order sellBtct{
                .symbol = config->instrumentList[0],
                .price = btct.bid,
                .volume = volume,
                .side = 2,
            };

            Order buyBtcc{
                .symbol = config->instrumentList[1],
                .price = btcc.ask,
                .volume = volume,
                .side = 1,
            };

            Order buyUsdc{
                .symbol = config->instrumentList[2],
                .price = usdc.ask,
                .volume = volume,
                .side = 1,
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, sellBtct, buyBtcc, buyUsdc))
            {
                sentOrders[0] = sellBtct;
                sentOrders[1] = buyBtcc;
                sentOrders[2] = buyUsdc;
                fillMode = true;
                filled = 0u;
                PHOENIX_LOG_INFO(handler, "Taking case 2");
            }

            PHOENIX_LOG_INFO(handler, "[OPP CASE 2]", btct.bid.asDouble(), '>', btcc.ask.asDouble(), '*', usdc.ask.asDouble());
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
        case 0: 
        {
            logOrder("[NEW ORDER]", orderId, side, price, remaining); 
            auto const it = config->instrumentMap.find(symbol);
            PHOENIX_LOG_VERIFY(handler, (it != config->instrumentMap.end()), "Symbol", symbol, "doesn't exist");
            auto& sentOrder = sentOrders[it->second];
            sentOrder.orderId = orderId;
            sentOrder.isInFlight = false;
        }
        break;

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
            sentOrder.isInFlight = false;

            if (++filled == 3u)
            {
                fillMode = false;
                filled = 0u;
                PHOENIX_LOG_INFO(handler, "All orders filled");
                updatePnl();
            }
        }
        break;

        case 4: 
        {
            logOrder("[CANCELLED]", orderId, side, price, remaining);
            auto const it = config->instrumentMap.find(symbol);
            PHOENIX_LOG_VERIFY(handler, (it != config->instrumentMap.end()), "Symbol", symbol, "doesn't exist");
            auto& sentOrder = sentOrders[it->second];

            if (sentOrder.side == 1)
                sentOrder.price = bestPrices[it->second].ask;
            else
                sentOrder.price = bestPrices[it->second].bid;

            while (!handler->retrieve(tag::Stream::TakeMarketOrders{}, sentOrder));

            sentOrder.lastSent = std::chrono::steady_clock::now();
            sentOrder.isInFlight = false;
            PHOENIX_LOG_INFO(handler, "Retrying", symbol);
        }
        break;

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
        double btct = sentOrders[0].price.asDouble();
        double btcc = sentOrders[1].price.asDouble();
        double usdc = sentOrders[2].price.asDouble();

        double const contractSize = config->contractSize;
        double const volume = config->volumeSize;
        double const multiplier = contractSize * volume;

        // Buy BTC/T
        if (sentOrders[0].side == 1)
            pnl += ((btcc * usdc) - btct) * multiplier;

        // Buy BTC/C
        else
            pnl += (btct - (btcc * usdc)) * multiplier;

        PHOENIX_LOG_INFO(handler, "[PNL]", pnl, "USDT");
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
        Volume bidQty{};
        Volume askQty{};
    };

    RouterHandler<Router>* const handler;
    Config const* const config;

    std::array<InstrumentTopLevel, 3u> bestPrices;
    std::array<Order, 3u> sentOrders;

    bool fillMode = false;
    unsigned filled = 0u;

    double pnl = 0.0;
};

} // namespace phoenix::triangular
