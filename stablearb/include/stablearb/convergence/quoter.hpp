#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/data/fix.hpp"
#include "stablearb/data/quotes.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

// TODO: don't quote on an existing order level

namespace stablearb {

template<typename NodeBase>
struct Quoter : NodeBase
{
    using NodeBase::NodeBase;

    using Traits = NodeBase::Traits;
    using PriceType = NodeBase::Traits::PriceType;
    using VolumeType = NodeBase::Traits::VolumeType;

    // TODO: handle case where only one side is available - now it leads to fatal
    inline void handle(tag::Quoter::Quote, FIXReader&& topLevel, std::size_t seqNum)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        std::int64_t bidIdx = -1u;
        std::int64_t askIdx = -1u;

        auto const numUpdates = topLevel.getFieldSize("269");
        for (auto i = 0; i < numUpdates; ++i)
        {
            auto typeField = topLevel.getNumber<unsigned int>("269", i);

            if (typeField == 0)
                bidIdx = i;

            if (typeField == 1)
                askIdx = i;
        }

        PriceType lastBid = bestBid;
        PriceType lastAsk = bestAsk;
        VolumeType lastBidQty = bestBidQty;
        VolumeType lastAskQty = bestAskQty;

        if (bidIdx > -1)
        {
            bestBid = topLevel.getDecimal<PriceType>("270", bidIdx);
            bestBidQty = topLevel.getDecimal<VolumeType>("271", bidIdx);
            STABLEARB_LOG_VERIFY(handler, (!bestBid.error && !bestBidQty.error), "Decimal parse error");
        }

        if (askIdx > -1)
        {
            bestAsk = topLevel.getDecimal<PriceType>("270", askIdx);
            bestAskQty = topLevel.getDecimal<VolumeType>("271", askIdx);
            STABLEARB_LOG_VERIFY(handler, (!bestAsk.error && !bestAskQty.error), "Decimal parse error");
        }

        updateIndex(topLevel);

        auto tickSize = config->tickSize;
        auto lotSize = config->lotSize;
        auto aggressive = config->aggressive;

        // > lotSize to prevent trading on my own book event
        if (bestBid < 1.0 && bestBidQty > lotSize && lastBid != bestBid)
        {
            auto tickSize = config->tickSize;

            PriceType quotePrice = bestBid;

            if (aggressive)
            {
                PriceType aggressiveBid = bestBid + tickSize;
                VolumeType doubleLotSize = lotSize + lotSize;
                if (aggressiveBid < 1.0 && aggressiveBid < bestAsk)
                {
                    sendQuote({.price = aggressiveBid, .volume = lotSize, .side = 1});
                    sendQuote({.price = quotePrice, .volume = doubleLotSize, .side = 1});
                }
                else
                    sendQuote({.price = quotePrice, .volume = doubleLotSize, .side = 1});
            }
            else
                sendQuote({.price = quotePrice, .volume = lotSize, .side = 1});
        }

        if (bestAsk > 1.0 && bestAskQty > lotSize && lastAsk != bestAsk)
        {
            auto tickSize = config->tickSize;

            PriceType quotePrice = bestAsk;

            if (aggressive)
            {
                PriceType aggressiveAsk = bestAsk - tickSize;
                VolumeType doubleLotSize = lotSize + lotSize;
                if (aggressiveAsk > 1.0 && aggressiveAsk > bestBid)
                {
                    sendQuote({.price = aggressiveAsk, .volume = lotSize, .side = 2});
                    sendQuote({.price = quotePrice, .volume = doubleLotSize, .side = 2});
                }
                else
                    sendQuote({.price = quotePrice, .volume = doubleLotSize, .side = 2});
            }
            else
                sendQuote({.price = quotePrice, .volume = lotSize, .side = 2});
        }
    }

    inline void handle(tag::Quoter::ExecutionReport, FIXReader&& report)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();
        STABLEARB_LOG_INFO(handler, "Incoming execution report");

        auto status = report.getNumber<unsigned int>("39");
        auto orderId = report.getString("37");
        auto clOrderId = report.getString("41");
        auto remaining = report.getDecimal<VolumeType>("151");
        auto side = report.getNumber<unsigned int>("54");
        auto price = report.getDecimal<PriceType>("44");
        STABLEARB_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        // new order
        if (status == 0)
        {
            STABLEARB_LOG_INFO(
                handler,
                "New order:",
                orderId,
                "with",
                remaining.template as<double>(),
                '@',
                price.template as<double>());

            orders[orderId] = remaining;
            if (clOrderId.size() > 0 && clOrderId[0] != 't')
                handler->invoke(tag::Risk::UpdatePosition{}, remaining.template as<double>(), side);
        }

        // partial/total fill
        if (status == 1 || status == 2)
        {
            STABLEARB_LOG_INFO(
                handler,
                "Order filled:",
                orderId,
                "with clOrdId:",
                clOrderId,
                "with remaining",
                remaining.template as<double>(),
                '@',
                price.template as<double>());

            auto tickSize = config->tickSize;
            auto lastRemaining = orders[orderId];
            auto executed = lastRemaining - remaining;

            // if this is a take profit order, rebalance position
            if (clOrderId.size() > 0 && clOrderId[0] == 't')
                handler->invoke(tag::Risk::UpdatePosition{}, executed.template as<double>(), side == 1u ? 2u : 1u);
            else if (executed > 0)
            {
                unsigned int reversedSide = side == 1 ? 2 : 1;
                PriceType reversedPrice = side == 1 ? price + tickSize : price - tickSize;
                SingleQuote<Traits> quote{.price = reversedPrice, .volume = executed, .side = reversedSide};
                STABLEARB_LOG_INFO(
                    handler,
                    "Quoted take-profit",
                    (side == 2 ? "bid" : "ask"),
                    reversedPrice.template as<double>(),
                    '@',
                    quote.price.template as<double>());
            }

            if (remaining.getValue() == 0)
                orders.erase(orderId);
            else
                orders[orderId] = remaining;
        }

        // cancelled
        if (status == 4)
        {
            orders.erase(orderId);
            STABLEARB_LOG_WARN(
                handler, "Order cancelled:", orderId, "with remaining quantity", remaining.template as<double>());

            handler->invoke(tag::Risk::UpdatePosition{}, remaining.template as<double>(), side == 1u ? 2u : 1u);
        }

        // rejected
        if (status == 8)
        {
            auto reason = report.getNumber<unsigned int>("103");
            orders.erase(orderId);
            STABLEARB_LOG_ERROR(handler, "Order rejected:", orderId, "due to reason", reason);

            handler->invoke(tag::Risk::UpdatePosition{}, remaining.template as<double>(), side == 1u ? 2u : 1u);
        }
    }

private:
    void sendQuote(SingleQuote<Traits> quote)
    {
        auto* handler = this->getHandler();

        if (!handler->retrieve(tag::Risk::Check{}, quote))
            return;

        handler->invoke(tag::Stream::SendQuote{}, quote);
        STABLEARB_LOG_INFO(
            handler,
            "Quoted",
            quote.side == 1 ? "bid" : "ask",
            quote.volume.template as<double>(),
            '@',
            quote.price.template as<double>());
    }

    void updateIndex(FIXReader& topLevel)
    {
        PriceType newIndex = topLevel.getDecimal<PriceType>("100090");
        if (index != newIndex && newIndex.getValue() != 0u)
        {
            index = newIndex;
            STABLEARB_LOG_INFO(this->getHandler(), "Index price changed to", index.template as<double>());
        }
    }

    PriceType lastQuote;
    PriceType bestBid;
    PriceType bestAsk;
    PriceType index;
    VolumeType bestBidQty;
    VolumeType bestAskQty;

    // <order id, remaining volume>
    boost::unordered_flat_map<std::string, VolumeType> orders;
};

} // namespace stablearb
