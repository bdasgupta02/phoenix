#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace phoenix::convergence {

template<typename NodeBase>
struct Quoter : NodeBase
{
    using NodeBase::NodeBase;

    using Traits = NodeBase::Traits;
    using PriceType = NodeBase::Traits::PriceType;
    using VolumeType = NodeBase::Traits::VolumeType;
    using PriceValue = PriceType::ValueType;
    using Order = SingleOrder<Traits>;

    inline void handle(tag::Quoter::MDUpdate, FIXReader& marketData)
    {
        auto* handler = this->getHandler();        
        auto* config = this->getConfig();

        //////// GET PRICES
        PriceType bestBid;
        PriceType bestAsk;

        std::size_t const numUpdates = marketData.getNumber<std::size_t>("268");
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            if (typeField == 0u)
                bestBid.minOrZero(marketData.getDecimal<PriceType>("270", i));
            if (typeField == 1u)
                bestAsk.minOrZero(marketData.getDecimal<PriceType>("270", i));
        }

        //////// TRIGGER
        PriceType const tickSize = config->tickSize;
        VolumeType const lotSize = config->lotSize;

        if (bestBid)
        {
            if (lastBid.price < bestBid && !lastBid.isInFlight && lastBid.price)
            {
                lastBid.isActive = false;
                while (!handler->retrieve(tag::Stream::CancelQuote{}, lastBid.orderId));
                PHOENIX_LOG_INFO(handler, "Cancelling stale order", lastBid.orderId);
            }

            if (lastBid.price < bestBid && bestBid < TAKE_PROFIT_ASK - 0.0001)
            {
                sendQuote({
                    .symbol = config->instrument,
                    .price = bestBid + 0.0001,
                    .volume = lotSize,
                    .side = lastBid.side
                });
            }
        }

        if (bestAsk)
        {
            if (lastAsk.price > bestAsk && !lastAsk.isInFlight && lastAsk.price)
            {
                lastAsk.isActive = false;
                while (!handler->retrieve(tag::Stream::CancelQuote{}, lastAsk.orderId));
                PHOENIX_LOG_INFO(handler, "Cancelling stale order", lastAsk.orderId);
            }

            if (lastAsk.price > bestAsk && bestAsk > TAKE_PROFIT_BID + 0.0001)
            {
                sendQuote({
                    .symbol = config->instrument,
                    .price = bestAsk - 0.0001,
                    .volume = lotSize,
                    .side = lastAsk.side
                });
            }
        }
    }
    
    inline void handle(tag::Quoter::ExecutionReport, FIXReader& report)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto const& symbol = report.getString("55");
        auto status = report.getNumber<unsigned>("39");
        auto const& orderId = report.getString("11");
        auto const& clOrderId = report.getString("41");
        auto remaining = report.getDecimal<VolumeType>("151");
        auto justExecuted = report.getDecimal<VolumeType>("14");
        auto side = report.getNumber<unsigned>("54");
        auto price = report.getDecimal<PriceType>("44");
        PHOENIX_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        bool const isTakeProfit = clOrderId.size() == 0 || clOrderId[0] == 't';
        auto& sentOrder = side == 1 ? lastBid : lastAsk;

        if (symbol != config->instrument)
        {
            PHOENIX_LOG_WARN(handler, "Incorrect instrument", symbol);
            return;
        }
         
        switch (status)
        {
        case 0: 
        {
            logOrder("[NEW ORDER]", orderId, side, price, remaining); 
            if (!isTakeProfit)
            {
                sentOrder.orderId = orderId;
                sentOrder.isInFlight = false;
                sentOrder.isActive = true;
            }
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

            if (isTakeProfit)
            {
                if (side == 1)
                {
                    edgeCaptured += (PriceType{totalAskPrices / totalAskNum} - TAKE_PROFIT_BID) * config->lotSize;
                    PHOENIX_LOG_INFO(handler, "[EDGE CAPTURED]", edgeCaptured.asDouble());
                }
                else 
                {
                    edgeCaptured += (TAKE_PROFIT_ASK - PriceType{totalBidPrices / totalBidNum}) * config->lotSize;
                    PHOENIX_LOG_INFO(handler, "[EDGE CAPTURED]", edgeCaptured.asDouble());
                }
            }
            else 
            {
                sentOrder.isFilled = true;
                sentOrder.price = avgFillPrice;
                sentOrder.isInFlight = false;
                sentOrder.isActive = false;
                auto& takeProfit = side == 1 ? lastAskTP : lastBidTP;
                takeProfit.volume = config->lotSize;
                sendQuote(takeProfit);
            }
        }
        break;

        case 4: 
        {
            logOrder("[CANCELLED]", orderId, side, price, remaining);
            if (isTakeProfit)
            {
                if (side == 1)
                    sendQuote(lastAskTP, false);
                else 
                    sendQuote(lastBidTP, false);
            }
        }
        break;

        case 8:
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
        }
        break;

        default: PHOENIX_LOG_WARN(handler, "Other status type", status); break;
        }
    }

private:
    void sendQuote(SingleOrder<Traits> const& quote, bool isNormal = true)
    {
        auto* handler = this->getHandler();

        if (isNormal)
        {
            auto& order = quote.side == 1 ? lastBid : lastAsk;
            if (order.isInFlight) [[unlikely]]
            {
                PHOENIX_LOG_WARN(handler, "[IN-FLIGHT] Quote request ignored due to in-flight quote");
                return;
            }

            if (handler->retrieve(tag::Stream::SendQuotes{}, quote))
                order = quote;
            else 
                return;
        }
        else 
        {
            while (!handler->retrieve(tag::Stream::SendQuotes{}, quote));
        }
        
        PHOENIX_LOG_INFO(
            handler,
            "[QUOTED]",
            quote.takeProfit ? "[TAKE PROFIT]" : "",
            quote.side == 1 ? "BID" : "ASK",
            quote.volume.asDouble(),
            '@',
            quote.price.asDouble());

    }
    
    inline void logOrder(
        std::string_view type,
        std::string_view orderId,
        unsigned side,
        PriceType price,
        VolumeType volume,
        std::string_view rejectReason = "")
    {
        PHOENIX_LOG_INFO(
            this->getHandler(),
            type,
            orderId,
            side == 1 ? "BUY" : "SELL",
            volume.asDouble(),
            '@',
            price.asDouble(),
            rejectReason.empty() ? "" : "with reason",
            rejectReason);
    }

    static constexpr VolumeType TAKE_PROFIT_BID{1.0001};
    static constexpr VolumeType TAKE_PROFIT_ASK{0.9999};

    Order lastBid{.side = 1, .isActive = false, .isInFlight = false};
    Order lastAsk{.side = 1, .isActive = false, .isInFlight = false};
    Order lastBidTP{.price = TAKE_PROFIT_BID, .side = 1, .takeProfit = true};
    Order lastAskTP{.price = TAKE_PROFIT_ASK, .side = 2, .takeProfit = true};

    double totalBidPrices = 0.0;
    double totalBidNum = 0.0;

    double totalAskPrices = 0.0;
    double totalAskNum = 0.0;

    PriceType edgeCaptured{0.0};
};

/*
template<typename NodeBase>
struct Quoter : NodeBase
{
    using NodeBase::NodeBase;

    using Traits = NodeBase::Traits;
    using PriceType = NodeBase::Traits::PriceType;
    using VolumeType = NodeBase::Traits::VolumeType;
    using PriceValue = PriceType::ValueType;

    struct OrderRecord
    {
        PriceType price;
        VolumeType remaining;
        std::string orderId;
        bool isTakeProfit = false;
        bool isActive = false;
    };

    using OrderRecords = boost::unordered::unordered_flat_map<PriceValue, OrderRecord>;

    inline void handle(tag::Quoter::MDUpdate, FIXReader& marketData)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        //////// GET PRICES

        PriceType bestBid;
        PriceType bestAsk;

        std::size_t const numUpdates = marketData.getNumber<std::size_t>("268");
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            if (typeField == 0u)
                bestBid.minOrZero(marketData.getDecimal<PriceType>("270", i));
            if (typeField == 1u)
                bestAsk.minOrZero(marketData.getDecimal<PriceType>("270", i));
        }

        PriceType const tickSize = config->tickSize;
        VolumeType const lotSize = config->lotSize;
        bool const aggressive = config->aggressive;

        //////// TRIGGER

        if (bestBid && bestBid < 1.0 && lastBid != bestBid && !hasBeenOrdered(bestBid))
        {
            if (lastOrderedBid.isActive && lastOrderedBid.price < bestBid)
            {
                // cancel stale order
                cancelOrder(lastOrderedBid);
                lastOrderedBid.isActive = false;
            }

            if (lastOrderedBid.isActive && lastOrderedBid.price > bestBid) [[unlikely]]
                PHOENIX_LOG_WARN(handler, "Previous bid is better");
            else if (aggressive) [[likely]]
            {
                PriceType const aggressiveBid = bestBid + tickSize;
                if (aggressiveBid < 1.0 && aggressiveBid < bestAsk && aggressiveBid < bestAsk &&
                    !hasBeenOrdered(aggressiveBid))
                    sendQuote({.price = aggressiveBid, .volume = lotSize, .side = 1});
                else
                    sendQuote({.price = bestBid, .volume = lotSize, .side = 1});
            }
            else
                sendQuote({.price = bestBid, .volume = lotSize, .side = 1});
        }

        if (bestAsk && bestAsk > 1.0 && lastAsk != bestAsk && !hasBeenOrdered(bestAsk))
        {
            if (lastOrderedAsk.isActive && lastOrderedAsk.price > bestAsk)
            {
                // cancel stale order
                cancelOrder(lastOrderedAsk);
                lastOrderedAsk.isActive = false;
            }

            if (lastOrderedAsk.isActive && lastOrderedAsk.price < bestAsk) [[unlikely]]
                PHOENIX_LOG_WARN(handler, "Previous ask is better");
            else if (aggressive) [[likely]]
            {
                PriceType const aggressiveAsk = bestAsk - tickSize;
                if (aggressiveAsk > 1.0 && aggressiveAsk > bestBid && aggressiveAsk > bestBid &&
                    !hasBeenOrdered(aggressiveAsk))
                    sendQuote({.price = aggressiveAsk, .volume = lotSize, .side = 2});
                else
                    sendQuote({.price = bestAsk, .volume = lotSize, .side = 2});
            }
            else
                sendQuote({.price = bestAsk, .volume = lotSize, .side = 2});
        }

        if (bestBid)
            lastBid = bestBid;
        if (bestAsk)
            lastAsk = bestAsk;
    }

    inline void handle(tag::Quoter::ExecutionReport, FIXReader& report)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto status = report.getNumber<unsigned int>("39");
        auto const& orderId = report.getString("11");
        auto const& clOrderId = report.getString("41");
        auto remaining = report.getDecimal<VolumeType>("151");
        auto totalExecuted = report.getDecimal<VolumeType>("14");
        auto side = report.getNumber<unsigned int>("54");
        auto price = report.getDecimal<PriceType>("44");
        PHOENIX_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        bool const isTakeProfit = clOrderId.size() == 0 || clOrderId[0] == 't';

        // new order
        if (status == 0)
        {
            logOrder("[NEW ORDER]", orderId, clOrderId, side, price, remaining);
            inflight.erase(price.getValue());
            if (side == 1)
            {
                bidsOrdered[price.getValue()] = {price, remaining, orderId, isTakeProfit};
                if (!isTakeProfit)
                    lastOrderedBid = {.orderId = orderId, .isActive = true};
            }
            else
            {
                asksOrdered[price.getValue()] = {price, remaining, orderId, isTakeProfit};
                if (!isTakeProfit)
                    lastOrderedAsk = {.orderId = orderId, .isActive = true};
            }
        }

        // partial/total fill
        if (status == 1 || status == 2)
        {
            logOrder("[FILL]", orderId, clOrderId, side, price, remaining);
            if (remaining != 0.0)
            {
                PHOENIX_LOG_INFO(handler, "Partial fill with remaining qty of", remaining.asDouble());
                return;
            }

            if (isTakeProfit)
            {
                if (side == 1)
                {
                    asksUncaptured -= remaining;
                    bidsOrdered.erase(price.getValue());
                }
                else
                {
                    bidsUncaptured -= remaining;
                    asksOrdered.erase(price.getValue());
                }
                capturedProfit += totalExecuted;
            }
            else
            {
                if (side == 1)
                {
                    bidsUncaptured += remaining;
                    bidsOrdered.erase(price.getValue());
                    lastOrderedBid.isActive = false;
                }
                else
                {
                    asksUncaptured += remaining;
                    asksOrdered.erase(price.getValue());
                    lastOrderedAsk.isActive = false;
                }

                auto tickSize = config->tickSize;
                unsigned int reversedSide = side == 1 ? 2 : 1;
                PriceType reversedPrice = side == 1 ? price + tickSize : price - tickSize;
                sendQuote({.price = reversedPrice, .volume = totalExecuted, .side = reversedSide, .takeProfit = true});
            }

            logStatus();
        }

        // cancelled
        if (status == 4)
        {
            logOrder("[CANCELLED]", orderId, clOrderId, side, price, remaining);
            if (side == 1)
                bidsOrdered.erase(price.getValue());
            else
                asksOrdered.erase(price.getValue());
        }

        // rejected
        if (status == 8)
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, clOrderId, side, price, remaining, reason);
            if (side == 1)
                bidsOrdered.erase(price.getValue());
            else
                asksOrdered.erase(price.getValue());
        }
    }

private:
    void logOrder(
        std::string_view type,
        std::string_view orderId,
        std::string_view clOrderId,
        unsigned int side,
        PriceType price,
        VolumeType remaining,
        std::string_view rejectReason = "")
    {
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(
            handler,
            type,
            orderId,
            clOrderId,
            side == 1 ? "BID" : "ASK",
            "with remaining",
            remaining.asDouble(),
            '@',
            price.asDouble(),
            rejectReason.empty() ? "" : "with reason",
            rejectReason);
    }

    void logStatus()
    {
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(
            handler,
            "[STATUS]",
            "Uncaptured bids:",
            bidsUncaptured.asDouble(),
            "Uncaptured asks:",
            asksUncaptured.asDouble(),
            "Captured profit:",
            capturedProfit.asDouble());
    }

    void sendQuote(SingleOrder<Traits> quote)
    {
        auto* handler = this->getHandler();

        if (inflight.contains(quote.price.getValue()))
        {
            PHOENIX_LOG_WARN(handler, "Level already quoted", quote.price.asDouble());
            return;
        }

        handler->invoke(tag::Stream::SendQuotes{}, quote);
        inflight.insert(quote.price.getValue());
        PHOENIX_LOG_INFO(
            handler,
            "[QUOTED]",
            quote.takeProfit ? "[TAKE PROFIT]" : "",
            quote.side == 1 ? "BID" : "ASK",
            quote.volume.asDouble(),
            '@',
            quote.price.asDouble());
    }

    void cancelOrder(OrderRecord order)
    {
        auto* handler = this->getHandler();
        handler->invoke(tag::Stream::CancelQuote{}, order.orderId);
        PHOENIX_LOG_INFO(handler, "Cancelling stale order", order.orderId);
    }

    bool hasBeenOrdered(PriceType price)
    {
        return bidsOrdered.contains(price.getValue()) || asksOrdered.contains(price.getValue());
    }

    PriceType lastBid;
    PriceType lastAsk;

    boost::unordered::unordered_flat_set<PriceValue> inflight;

    OrderRecords bidsOrdered;
    OrderRecords asksOrdered;
    OrderRecord lastOrderedBid;
    OrderRecord lastOrderedAsk;

    VolumeType capturedProfit = 0.0;
    VolumeType bidsUncaptured = 0.0;
    VolumeType asksUncaptured = 0.0;
};
*/

} // namespace phoenix::convergence
