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

        auto const checkQty = [it, this](double qty)
        { return it->second == 1 || ((it->second == 0 || it->second == 2) && qty > qtyThreshold); };

        ///////// UPDATE PRICES
        Price newBid;
        Price newAsk;
        std::size_t const numUpdates = marketData.getNumber<std::size_t>("268");
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            unsigned const qty = marketData.getNumber<double>("270", i);
            bool const validQty = checkQty(qty);

            if (typeField == 0u && validQty)
                newBid.minOrZero(marketData.getDecimal<Price>("270", i));
            if (typeField == 1u && validQty)
                newAsk.minOrZero(marketData.getDecimal<Price>("270", i));

            if (!validQty)
                PHOENIX_LOG_WARN(handler, "Skipping tiny entry of", qty, "units for", symbol);
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
        // TODO: min gap should be contract size
        if (!update || fillMode)
            return;

        PHOENIX_LOG_VERIFY(handler, (instrumentPrices.bid < instrumentPrices.ask), "Overlapping prices");

        auto& eth = bestPrices[0];
        auto& steth = bestPrices[2];
        auto& bridge = bestPrices[1];

        double const volume = config->volumeSize;

        // Buy ETH, Buy STETH for ETH, Sell STETH
        if (steth.bid - threshold > (eth.ask * bridge.ask))
        {
            PHOENIX_LOG_INFO(
                handler, "[OPP CASE 1]", steth.bid.asDouble(), '>', eth.ask.asDouble(), '*', bridge.ask.asDouble());

            // clang-format off
            Order buyEth{
                .symbol = config->instrumentList[0],
                .volume = volume,
                .side = 1,
                .isLimit = false
            };

            Order sellSteth{
                .symbol = config->instrumentList[2],
                .volume = volume,
                .side = 2,
                .isLimit = false
            };

            Order buyBridge{
                .symbol = config->instrumentList[1],
                .volume = volume,
                .side = 1,
                .isLimit = false
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, buyEth, sellSteth, buyBridge))
            {
                sentOrders[0] = buyEth;
                sentOrders[1] = buyBridge;
                sentOrders[2] = sellSteth;
                fillMode = true;
                filled = 0u;
            }
        }

        // Buy STETH, Sell STETH for ETH, Sell ETH
        if ((eth.bid * bridge.bid) - threshold > steth.ask)
        {
            PHOENIX_LOG_INFO(
                handler, "[OPP CASE 2]", eth.bid.asDouble(), '*', bridge.bid.asDouble(), '>', steth.ask.asDouble());

            // clang-format off
            Order buySteth{
                .symbol = config->instrumentList[2],
                .volume = volume,
                .side = 1,
                .isLimit = false
            };

            Order sellEth{
                .symbol = config->instrumentList[0],
                .volume = volume,
                .side = 2,
                .isLimit = false
            };

            Order sellBridge{
                .symbol = config->instrumentList[1],
                .volume = volume,
                .side = 2,
                .isLimit = false
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, buySteth, sellEth, sellBridge))
            {
                sentOrders[0] = sellEth;
                sentOrders[1] = sellBridge;
                sentOrders[2] = buySteth;
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
        double eth = sentOrders[0].price.asDouble();
        double steth = sentOrders[2].price.asDouble();
        double bridge = sentOrders[1].price.asDouble();

        double const contractSize = config->contractSize;
        double const volume = config->volumeSize;
        double const multiplier = contractSize * volume;

        if (sentOrders[0].side == 1)
            pnl += (steth - (eth * bridge)) * multiplier;
        else
            pnl += ((eth * bridge) - steth) * multiplier;

        PHOENIX_LOG_INFO(handler, "[PNL]", pnl, "USDC");
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
