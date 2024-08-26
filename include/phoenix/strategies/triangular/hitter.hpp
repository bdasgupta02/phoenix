#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/tags.hpp"

#include <array>
#include <cstdint>
#include <limits>

// TODO: balance check and sleep for 10 mins

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
    inline void handle(tag::Hitter::MDUpdate, FIXReader&& marketData, bool const update = true)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();
        [[maybe_unused]] auto timer = handler->retrieve(tag::Profiler::Guard{}, "Hitter MDUpdate");

        auto const& instrumentMap = config->instrumentMap;

        auto const& symbol = marketData.getString("55");
        auto const it = instrumentMap.find(symbol);
        PHOENIX_LOG_VERIFY(handler, (it != instrumentMap.end()), "Unknown instrument", symbol);

        ///////// UPDATE PRICES (all 2nd level to reduce risk of slippage)

        Price newBid;
        Price newAsk;

        std::size_t const numUpdates = marketData.getFieldSize("269");
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

        // BTC (with capital in USDC and USDT):
        // CASE 1 - buy BTC/USDC, sell BTC/USDT, buy USDC/USDT
        // CASE 2 - buy BTC/USDT, sell BTC/USDC, sell USDC/USDT

        auto& btcUsdt = bestPrices[0];
        auto& btcUsdc = bestPrices[2];
        auto& usdcUsdt = bestPrices[1];

        ///////// FILL MODE (exiting stale position)
        // disabled due to market orders

        /*if (fillMode)*/
        /*{*/
        /*    Order& btcUsdtSent = sentOrders[0];*/
        /*    Order& btcUsdcSent = sentOrders[2];*/
        /*    Order& usdcUsdtSent = sentOrders[1];*/
        /**/
        /*    // CASE 1*/
        /*    if (btcUsdtSent.side == 2)*/
        /*    {*/
        /*        // bid always retries first*/
        /*        if (!btcUsdcSent.isFilled)*/
        /*        {*/
        /*            btcUsdcSent.price = btcUsdc.ask;*/
        /*            handler->retrieve(tag::Stream::TakeMarketOrders{}, btcUsdtSent);*/
        /*        }*/
        /**/
        /*        if (!btcUsdtSent.isFilled)*/
        /*        {*/
        /*            btcUsdtSent.price = btcUsdt.bid;*/
        /*            handler->retrieve(tag::Stream::TakeMarketOrders{}, btcUsdtSent);*/
        /*        }*/
        /*    }*/
        /**/
        /*    // CASE 2*/
        /*    if (btcUsdtSent.side == 1)*/
        /*    {*/
        /*        // bid always retries first*/
        /*        if (!btcUsdtSent.isFilled)*/
        /*        {*/
        /*            btcUsdtSent.price = btcUsdt.ask;*/
        /*            handler->retrieve(tag::Stream::TakeMarketOrders{}, btcUsdtSent);*/
        /*        }*/
        /**/
        /*        if (!btcUsdcSent.isFilled)*/
        /*        {*/
        /*            btcUsdcSent.price = btcUsdc.bid;*/
        /*            handler->retrieve(tag::Stream::TakeMarketOrders{}, btcUsdcSent);*/
        /*        }*/
        /*    }*/
        /**/
        /*    return;*/
        /*}*/

        ///////// TRIGGER

        if (!update)
            return;

        double const triggerThreshold = config->triggerThreshold;

        // CASE 1
        if (btcUsdt.bid - triggerThreshold > btcUsdc.ask + triggerThreshold)
        {
            /*PHOENIX_LOG_INFO(handler, "[OPP CASE 1]", btcUsdt.bid.str(), btcUsdc.ask.str(), usdcUsdt.ask.str());*/

            double bridgeVolume = btcUsdt.bid.asDouble() * config->contractSize;
            if (usdtBalance < bridgeVolume || usdcBalance < bridgeVolume)
                return;

            // clang-format off
            Order sellBtcUsdt{
                .symbol = config->instrumentList[0],
                .volume = 1.0,
                .side = 2,
                .isLimit = false
            };

            Order buyBtcUsdc{
                .symbol = config->instrumentList[2],
                .volume = 1.0,
                .side = 1,
                .isLimit = false
            };

            Order limitBridge{
                .symbol = config->instrumentList[1],
                .price = 0.9999,
                .volume = bridgeVolume,
                .side = 1,
                .takeProfit = true
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, buyBtcUsdc, sellBtcUsdt, limitBridge))
            {
                sentOrders[0] = sellBtcUsdt;
                sentOrders[1] = limitBridge;
                sentOrders[2] = buyBtcUsdc;
                fillMode = true;
                filled = 0u;
                usdtBalance -= bridgeVolume;
            }
        }

        // CASE 2
        if (btcUsdc.bid - triggerThreshold > btcUsdt.ask + triggerThreshold)
        {
            /*PHOENIX_LOG_INFO(handler, "[OPP CASE 2]", btcUsdc.bid.str(), btcUsdt.ask.str(), usdcUsdt.bid.str());*/

            double bridgeVolume = btcUsdc.bid.asDouble() * config->contractSize;
            if (usdtBalance < bridgeVolume || usdcBalance < bridgeVolume)
                return;

            // clang-format off
            Order buyBtcUsdt{
                .symbol = config->instrumentList[0],
                .volume = 1.0,
                .side = 1,
                .isLimit = false
            };

            Order sellBtcUsdc{
                .symbol = config->instrumentList[2],
                .volume = 1.0,
                .side = 2,
                .isLimit = false
            };

            Order limitBridge{
                .symbol = config->instrumentList[1],
                .price = 1.0001,
                .volume = bridgeVolume,
                .side = 2,
                .takeProfit = true
            };
            // clang-format on

            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, buyBtcUsdt, sellBtcUsdc, limitBridge))
            {
                sentOrders[0] = buyBtcUsdt;
                sentOrders[1] = limitBridge;
                sentOrders[2] = sellBtcUsdc;
                fillMode = true;
                filled = 0u;
                usdcBalance -= bridgeVolume;
            }
        }
    }

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::ExecutionReport, FIXReader&& report)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto const& symbol = report.getString("55");
        auto status = report.getNumber<unsigned>("39");
        auto const& orderId = report.getString("11");
        auto const& clOrderId = report.getString("41");
        auto remaining = report.getDecimal<Volume>("151");
        auto justExecuted = report.getDecimal<Volume>("14");
        auto side = report.getNumber<unsigned>("54");
        auto price = report.getDecimal<Price>("44");
        PHOENIX_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        // new order
        if (status == 0)
            logOrder("[NEW ORDER]", orderId, side, price, remaining);

        // partial fill
        if (status == 1)
            logOrder("[PARTIAL FILL]", orderId, side, price, justExecuted);

        // total fill
        if (status == 2)
        {
            if (clOrderId.size() > 0 && clOrderId[0] != 't')
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

                logOrder("[TOTAL FILL MARKET]", orderId, side, avgFillPrice, justExecuted);

                auto const it = config->instrumentMap.find(symbol);
                PHOENIX_LOG_VERIFY(handler, (it != config->instrumentMap.end()), "Symbol", symbol, "doesn't exist");
                auto& sentOrder = sentOrders[it->second];
                sentOrder.isFilled = true;
                sentOrder.price = avgFillPrice;

                if (++filled == 2u)
                {
                    fillMode = false;
                    filled = 0u;
                    PHOENIX_LOG_INFO(handler, "All orders filled");
                    modifyUnrealizedPnl();
                    return;
                }
            }
            else
            {
                logOrder("[TOTAL FILL LIMIT]", orderId, side, price, justExecuted);
                // assumption: qty == 1 for base asset, and using USDC/USDT for bridge
                // e.g. this is gonna be 6 for BTC
                double bridgeVolume = std::round(bestPrices[0].bid.asDouble() * config->contractSize);
                if (side == 1)
                    usdcBalance += bridgeVolume;
                else
                    usdtBalance += bridgeVolume;
            }
        }

        // cancelled
        if (status == 4)
            logOrder("[CANCELLED]", orderId, side, price, remaining);

        // rejected
        if (status == 8)
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
        }
    }

    inline void handle(tag::Hitter::InitUSDBalances, double usdc, double usdt)
    {
        usdcBalance = usdc;
        usdtBalance = usdt;
        PHOENIX_LOG_INFO(this->getHandler(), "[USD BALANCES]", "USDC:", usdc, "USDT:", usdt);
    }

private:
    [[gnu::hot, gnu::always_inline]]
    inline void logOrder(
        std::string_view type,
        std::string_view orderId,
        unsigned side,
        Price price,
        Volume volume,
        std::string_view rejectReason = "")
    {
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(
            handler,
            type,
            orderId,
            side == 1 ? "BID" : "ASK",
            volume.asDouble(),
            '@',
            price.asDouble(),
            rejectReason.empty() ? "" : "with reason",
            rejectReason);
    }

    [[gnu::hot, gnu::always_inline]]
    inline void modifyUnrealizedPnl()
    {
        Order& btcUsdtSent = sentOrders[0];
        Order& btcUsdcSent = sentOrders[2];

        // CASE 1
        if (btcUsdtSent.side == 2)
        {
            double const priceDiff = btcUsdtSent.price.asDouble() - btcUsdcSent.price.asDouble();
            double const withContract = priceDiff * 0.0001;
            unrealizedPnl += withContract;
        }

        // CASE 2
        if (btcUsdtSent.side == 1)
        {
            double const priceDiff = btcUsdcSent.price.asDouble() - btcUsdtSent.price.asDouble();
            double const withContract = priceDiff * 0.0001;
            unrealizedPnl += withContract;
        }

        PHOENIX_LOG_INFO(this->getHandler(), "[PNL]", "Unrealized:", unrealizedPnl);
    }

    struct InstrumentTopLevel
    {
        Price bid{PriceValue{}};
        Price ask{std::numeric_limits<PriceValue>::max()};
    };

    std::array<InstrumentTopLevel, 3u> bestPrices;

    // since market orders are broken in API
    // TODO: potential race condition with exchange when retrying
    std::array<Order, 3u> sentOrders;
    bool fillMode = false;
    unsigned filled = 0u;

    double unrealizedPnl = 0.0;

    double usdcBalance;
    double usdtBalance;
};

} // namespace phoenix::triangular
