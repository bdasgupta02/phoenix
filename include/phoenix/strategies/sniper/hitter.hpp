#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/graph/router_handler.hpp"
#include "phoenix/tags.hpp"

#include <chrono>

namespace phoenix::sniper {

// Pickoff hitting strategy for spots
// if theo matches/crosses a side, send 2 limit orders
// round system and other side match/cross exit risk management
// place take-profit quote 1 tick below/above pickoff quote
// Confirmed this hypothesis on BTC/USDC

// TODO: add index comparison threshold for trigger

template<typename NodeBase>
struct Hitter : NodeBase
{
    using Router = NodeBase::Router;
    using Config = NodeBase::Config;
    using Traits = NodeBase::Traits;
    using Price = NodeBase::Traits::PriceType;
    using Volume = NodeBase::Traits::VolumeType;
    using Order = SingleOrder<Traits>;

    Hitter(Config const& config, RouterHandler<Router>& handler)
        : NodeBase(config, handler)
        , config{&config}
        , handler{&handler}
    {}

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::MDUpdate, FIXReader& marketData, bool const update = true)
    {
        ///////// UPDATE PRICES

        Price newBid;
        Price newAsk;
        Price newIndex;

        std::size_t const numUpdates = marketData.getNumber<std::size_t>("268");
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            if (typeField == 0u)
                newBid.minOrZero(marketData.getDecimal<Price>("270", i));
            if (typeField == 1u)
                newAsk.minOrZero(marketData.getDecimal<Price>("270", i));
            if (typeField == 2u)
                newIndex.minOrZero(marketData.getDecimal<Price>("270", i));
        }

        if (newBid)
            bestBid = newBid;
        if (newAsk)
            bestAsk = newAsk;
        if (newIndex)
            bestIndex = newIndex;

        ///////// EXITING POSITION

        if (fillMode)
        {
            if (std::chrono::steady_clock::now() - lastOrdered >= EXIT_TIME)
            {
                switch (filled)
                {
                // none filled
                case 0u:
                    PHOENIX_LOG_INFO(handler, "Cancelling both unfilled orders");
                    handler->invoke(tag::Stream::CancelQuote{}, sentBid.orderId);
                    handler->invoke(tag::Stream::CancelQuote{}, sentAsk.orderId);
                    break;

                // 1 filled (problem)
                case 1u:
                    PHOENIX_LOG_WARN(handler, "Exiting one sided stale order");

                    if (!sentBid.isFilled)
                    {
                        handler->invoke(tag::Stream::CancelQuote{}, sentBid.orderId);
                        sentBid.isLimit = false;
                        handler->invoke(tag::Stream::TakeMarketOrders{}, sentBid);
                    }

                    if (!sentAsk.isFilled)
                    {
                        handler->invoke(tag::Stream::CancelQuote{}, sentAsk.orderId);
                        sentAsk.isLimit = false;
                        handler->invoke(tag::Stream::TakeMarketOrders{}, sentAsk);
                    }
                    break;

                default: PHOENIX_LOG_FATAL(handler, "Invalid filled value:", filled); break;
                };
            }

            return;
        }

        ///////// TRIGGER

        if (!update)
            return;

        Price const tickSize = config->tickSize;

        // Case 1: ask on best bid level, bid on best ask - 1 tick size
        if (bestIndex < bestBid - (tickSize.asDouble() * 10.0))
            quoteSpread(bestBid - tickSize, bestBid);

        // Case 2: bid on best ask level, ask on best bid + tick size
        if (bestIndex > bestAsk + (tickSize.asDouble() * 10.0))
            quoteSpread(bestAsk, bestAsk + tickSize);
    }

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::ExecutionReport, FIXReader& report)
    {
        auto const& symbol = report.getString("55");
        auto status = report.getNumber<unsigned>("39");
        auto const& orderId = report.getString("11");
        auto const& clOrderId = report.getString("41");
        auto remaining = report.getDecimal<Volume>("151");
        auto executed = report.getDecimal<Volume>("14");
        auto side = report.getNumber<unsigned>("54");
        auto price = report.getDecimal<Price>("44");
        PHOENIX_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        switch (status)
        {
        case 1: logOrder("[PARTIAL FILL]", orderId, side, price, remaining); break;

        case 4:
        {
            logOrder("[CANCELLED]", orderId, side, price, remaining);
            if (--filled == 0u)
                fillMode = false;
        }
        break;

        case 0:
        {
            logOrder("[NEW ORDER]", orderId, side, price, remaining);
            fillMode = true;
            if (side == 1)
                sentBid.orderId = orderId;
            else
                sentAsk.orderId = orderId;
        }
        break;

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

            logOrder("[FILL]", orderId, side, avgFillPrice, remaining);

            if (side == 1)
            {
                sentBid.isFilled = true;
                sentBid.price = avgFillPrice;
            }
            else
            {
                sentAsk.isFilled = true;
                sentAsk.price = avgFillPrice;
            }

            if (++filled == 2u)
            {
                fillMode = false;
                filled = 0u;
                auto qty = config->lots;
                pnlQty += (sentAsk.price - sentBid.price).asDouble() * qty.asDouble();
                PHOENIX_LOG_INFO(handler, "All orders filled with pnl", pnlQty, "(in contract size)");
            }
        }
        break;

        case 8:
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
            if (--filled == 0u)
                fillMode = false;
        }
        break;

        default: PHOENIX_LOG_WARN(handler, "Other status type", status); break;
        };
    }

private:
    [[gnu::hot, gnu::always_inline]]
    inline void logOrder(
        std::string_view type,
        std::string_view orderId,
        unsigned side,
        Price price,
        Volume remaining,
        std::string_view rejectReason = "")
    {
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(
            handler,
            type,
            orderId,
            side == 1 ? "BID" : "ASK",
            "with remaining",
            remaining.asDouble(),
            '@',
            price.asDouble(),
            rejectReason.empty() ? "" : "with reason",
            rejectReason);
    }

    [[gnu::hot, gnu::always_inline]]
    inline void quoteSpread(Price bidPrice, Price askPrice)
    {
        Volume const volume = config->lots;

        Order bid{.symbol = config->instrument, .price = bidPrice, .volume = volume, .side = 1};
        Order ask{.symbol = config->instrument, .price = askPrice, .volume = volume, .side = 2};

        if (handler->retrieve(tag::Stream::SendQuotes{}, bid, ask))
        {
            fillMode = true;
            filled = 0u;
            sentBid = bid;
            sentAsk = ask;
            lastOrdered = std::chrono::steady_clock::now();
        }
    }

    RouterHandler<Router>* const handler;
    Config const* const config;

    // best prices
    Price bestBid;
    Price bestAsk;
    Price bestIndex;

    // fill mode
    static constexpr std::chrono::seconds EXIT_TIME{15u};
    bool fillMode = false;
    unsigned filled = 0u;
    Order sentBid;
    Order sentAsk;
    std::chrono::steady_clock::time_point lastOrdered;

    // analysis
    double pnlQty = 0.0;
};

} // namespace phoenix::sniper
