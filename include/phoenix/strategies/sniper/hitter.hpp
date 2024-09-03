#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/graph/router_handler.hpp"
#include "phoenix/tags.hpp"

#include <chrono>

#include <immintrin.h>

namespace phoenix::sniper {

// Pickoff hitting strategy for spots
// if theo matches/crosses a side, send 2 limit orders
// round system and other side match/cross exit risk management
// place take-profit quote 1 tick below/above pickoff quote
// Confirmed this hypothesis on BTC/USDC

// TODO: add index comparison threshold for trigger

template<typename Price, std::size_t WindowSize>
struct RollingAverage
{
    void add(Price value)
    {
        if (filled)
            sum -= values[index];

        values[index] = value;
        sum += value;
        
        index = (index + 1u) % WindowSize;
        if (index == 0u)
            filled = true;
    }

    Price get() const
    {
        return sum / (filled ? Price{static_cast<double>(WindowSize)} : Price{static_cast<double>(index)});
    }

    std::array<Price, WindowSize> values{};
    std::size_t index = 0u;
    Price sum = 0.0;
    bool filled = false;
};

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

        bestIndex.minOrZero(marketData.getDecimal<Price>("100090"));

        std::size_t const numUpdates = marketData.getNumber<std::size_t>("268");
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            if (typeField == 0u)
                newBid.minOrZero(marketData.getDecimal<Price>("270", i));
            if (typeField == 1u)
                newAsk.minOrZero(marketData.getDecimal<Price>("270", i));
        }

        if (newBid)
            bestBid = newBid;
        if (newAsk)
            bestAsk = newAsk;
        
        if (bestIndex)
            avgIndex.add(bestIndex);

        ///////// EXITING POSITION

        if (fillMode)
        {
            // TODO: exit when index recovers back to mid
            if (std::chrono::steady_clock::now() - lastOrdered >= EXIT_TIME)
            {
                switch (filled)
                {
                // none filled
                case 0u:
                {
                    PHOENIX_LOG_INFO(handler, "Cancelling both unfilled orders");
                    handler->invoke(tag::Stream::CancelQuote{}, sentBid.orderId);
                    handler->invoke(tag::Stream::CancelQuote{}, sentAsk.orderId);
                    fillMode = false;
                    filled = 0u;
                }
                break;

                // 1 filled (problem)
                // can't take market orders due to wide spreads on deribit spots
                case 1u:
                {
                    PHOENIX_LOG_WARN(handler, "Exiting one sided stale order, retrying..");

                    Price const tickSize = config->tickSize;
                    
                    if (!sentBid.isFilled)
                    {
                        handler->invoke(tag::Stream::CancelQuote{}, sentBid.orderId);
                        if (sentAsk.price > bestBid)
                            sentBid.price = bestBid;
                        else 
                            sentBid.price = bestBid - (tickSize * 5.0);
                        while (!handler->retrieve(tag::Stream::SendQuotes{}, sentBid))
                            _mm_pause();
                    }

                    if (!sentAsk.isFilled)
                    {
                        handler->invoke(tag::Stream::CancelQuote{}, sentAsk.orderId);
                        if (sentBid.price < bestAsk)
                            sentAsk.price = bestAsk;
                        else 
                            sentAsk.price = bestAsk + (tickSize * 5.0);
                        while (!handler->retrieve(tag::Stream::SendQuotes{}, sentAsk))
                            _mm_pause();
                    }
                    lastOrdered = std::chrono::steady_clock::now();
                }
                break;

                default: PHOENIX_LOG_FATAL(handler, "Invalid filled value:", filled); break;
                };
            }

            return;
        }

        ///////// TRIGGER

        if (!update || !bestIndex)
        {
            PHOENIX_LOG_WARN(handler, "Update or index invalid");
            return;
        }

        if (!avgIndex.filled)
            return;

        Price const tickSize = config->tickSize;

        // Simple diming market maker
        Price const raIndex = avgIndex.get();
        double const diff = bestIndex.asDouble() - raIndex.asDouble();
        if (raIndex < bestIndex)
        {
            PHOENIX_LOG_INFO(handler, "[OPP CASE 1] avg:", raIndex.asDouble(), "best:", bestIndex.asDouble());
            Price const bidding = bestIndex - (tickSize * diff * 10.0);
            Price const asking = bestIndex + (tickSize * diff * 30.0);
            quoteSpread(bidding, asking);
        }
        else if (raIndex > bestIndex)
        {
            PHOENIX_LOG_INFO(handler, "[OPP CASE 2] avg:", raIndex.asDouble(), "best:", bestIndex.asDouble());
            Price const bidding = bestIndex - (tickSize * diff * 30.0);
            Price const asking = bestIndex + (tickSize * diff * 10.0);
            quoteSpread(bidding, asking);
        }
        else 
        {
            Price const bidding = bestIndex - (tickSize * 30.0);
            Price const asking = bestIndex + (tickSize * 30.0);
            quoteSpread(bidding, asking);
        }

        /*double const indexDouble = bestIndex.asDouble();*/
        /*double const bidDouble = bestBid.asDouble();*/
        /*double const askDouble = bestAsk.asDouble();*/
        /**/
        /*// Case 1: ask on best bid level, bid on best bid - x * tick size*/
        /*if (indexDouble < bidDouble - (tickSize.asDouble() * 20.0))*/
        /*{*/
        /*    Price const bidding = bestBid - (tickSize * 9.0);*/
        /*    Price const asking = bestBid;*/
        /*    if (bidding < asking && case1Streak >= 20u)*/
        /*    {*/
        /*        PHOENIX_LOG_INFO(handler, "[OPP CASE 1] with index at", indexDouble, "BID", bidDouble, "ASK", askDouble);*/
        /*        quoteSpread(bidding, asking);*/
        /*    }*/
        /*    ++case1Streak;*/
        /*}*/
        /*else*/
        /*    case1Streak = 0u; */
        /**/
        /*// Case 2: bid on best ask level, ask on best ask + x * tick size*/
        /*if (indexDouble > bestAsk.asDouble() + (tickSize.asDouble() * 20.0))*/
        /*{*/
        /*    Price const bidding = bestAsk;*/
        /*    Price const asking = bestAsk + (tickSize * 9.0);*/
        /*    if (bidding < asking && case2Streak >= 20u)*/
        /*    {*/
        /*        PHOENIX_LOG_INFO(handler, "[OPP CASE 2] with index at", indexDouble, "BID", bidDouble, "ASK", askDouble);*/
        /*        quoteSpread(bidding, asking);*/
        /*    }*/
        /*    ++case2Streak;*/
        /*}*/
        /*else */
        /*    case2Streak = 0u; */
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
        case 1:
            logOrder("[PARTIAL FILL]", orderId, side, price, remaining);
            break;
        case 4: 
            logOrder("[CANCELLED]", orderId, side, price, remaining);
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
                auto const& instrument = config->instrument;
                pnl += sentAsk.price.asDouble() - sentBid.price.asDouble();
                PHOENIX_LOG_INFO(handler, "All orders filled with PNL (in edge)", pnl, instrument);
                PHOENIX_LOG_VERIFY(handler, (pnl > -150), "Too much loss");
            }
        }
        break;

        case 8:
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
            if (side == 1 && sentBid.isActive)
                sentBid.isActive = false;
            else if (sentAsk.isActive)
                sentAsk.isActive = false;

            if (!sentBid.isActive && !sentAsk.isActive)
            {
                fillMode = false;
                filled = 0u;
            }
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
    static constexpr std::chrono::milliseconds EXIT_TIME{4000u};
    // static constexpr std::chrono::seconds EXIT_TIME{20u};
    bool fillMode = false;
    unsigned filled = 0u;
    Order sentBid;
    Order sentAsk;
    std::chrono::steady_clock::time_point lastOrdered;

    // triggers 
    std::size_t case1Streak = 0u;
    std::size_t case2Streak = 0u;

    RollingAverage<Price, 80u> avgIndex;

    // analysis
    double pnl = 0.0;
};

} // namespace phoenix::sniper
