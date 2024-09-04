#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/orders.hpp"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <cstdint>
#include <functional>
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

    struct OrderRecord
    {
        PriceType price;
        VolumeType remaining;
        std::string orderId;
        bool isTakeProfit = false;
        bool isActive = false;
    };

    using OrderRecords = boost::unordered::unordered_flat_map<PriceValue, OrderRecord>;

    inline void handle(tag::Quoter::MDUpdate, FIXReader&& marketData)
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

    inline void handle(tag::Quoter::ExecutionReport, FIXReader&& report)
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

} // namespace phoenix::convergence
