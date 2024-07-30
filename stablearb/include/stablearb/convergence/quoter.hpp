#pragma once

#include "stablearb/common/logger.hpp"
#include "stablearb/data/fix.hpp"
#include "stablearb/data/quotes.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

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

        PriceType lastBid = bestBid;
        PriceType lastAsk = bestAsk;
        VolumeType lastBidQty = bestBidQty;
        VolumeType lastAskQty = bestAskQty;

        if (topLevel.getNumber<unsigned int>("269", 0) == 0)
        {
            bestBid = topLevel.getDecimal<PriceType>("270", 0);
            bestAsk = topLevel.getDecimal<PriceType>("270", 1);
            bestBidQty = topLevel.getDecimal<VolumeType>("271", 0);
            bestAskQty = topLevel.getDecimal<VolumeType>("271", 1);
            STABLEARB_LOG_VERIFY(
                handler,
                (!bestBid.error && !bestAsk.error && !bestBidQty.error && !bestAskQty.error),
                "Price parse error");
        }
        else if (topLevel.getNumber<unsigned int>("269", 1) == 0)
        {
            bestBid = topLevel.getDecimal<PriceType>("270", 1);
            bestAsk = topLevel.getDecimal<PriceType>("270", 0);
            bestBidQty = topLevel.getDecimal<VolumeType>("271", 1);
            bestAskQty = topLevel.getDecimal<VolumeType>("271", 0);
            STABLEARB_LOG_VERIFY(
                handler,
                (!bestBid.error && !bestAsk.error && !bestBidQty.error && !bestAskQty.error),
                "Price parse error");
        }
        else
        {
            STABLEARB_LOG_ERROR(handler, "Invalid top level market data: cannot find bid");
            return;
        }

        auto tickSize = config->tickSize;
        auto lotSize = config->lotSize;

        // > lotSize to prevent trading on my own book event
        if (bestBid < 1.0 && bestBidQty > lotSize && lastBid != bestBid)
        {
            auto tickSize = config->tickSize;

            PriceType quotePrice = bestBid;
            PriceType aggressiveBid = bestBid + tickSize;
            if (aggressiveBid < 1.0 && aggressiveBid < bestAsk)
                quotePrice = aggressiveBid;

            SingleQuote<Traits> quote{.price = quotePrice, .volume = config->lotSize, .side = 1};

            if (!handler->retrieve(tag::Risk::Check{}, quote))
                return;

            handler->invoke(tag::Stream::SendQuote{}, quote);
            STABLEARB_LOG_INFO(
                handler, "Quoted bid", quote.volume.template as<double>(), '@', bestBid.template as<double>());
        }

        if (bestAsk > 1.0 && bestAskQty > lotSize && lastAsk != bestAsk)
        {
            auto tickSize = config->tickSize;

            PriceType quotePrice = bestAsk;
            PriceType aggressiveAsk = bestAsk - tickSize;
            if (aggressiveAsk > 1.0 && aggressiveAsk > bestBid)
                quotePrice = aggressiveAsk;

            SingleQuote<Traits> quote{.price = quotePrice, .volume = config->lotSize, .side = 2};

            if (!handler->retrieve(tag::Risk::Check{}, quote))
                return;

            handler->invoke(tag::Stream::SendQuote{}, quote);
            STABLEARB_LOG_INFO(
                handler, "Quoted ask", quote.volume.template as<double>(), '@', bestAsk.template as<double>());
        }
    }

    inline void handle(tag::Quoter::ExecutionReport, FIXReader&& report)
    {
        auto* handler = this->getHandler();
        auto status = report.getNumber<unsigned int>("39");
        auto orderId = report.getString("37");

        // new order
        if (status == 0)
        {
            auto price = report.getStringView("44");
            auto remaining = report.getDecimal<VolumeType>("151");

            orders[orderId] = remaining;
            STABLEARB_LOG_INFO(
                handler, "New order:", orderId, "with remaining", remaining.template as<double>(), '@', price);
        }

        // partial/total fill
        if (status == 1 || status == 2)
        {
            auto price = PriceType{report.getString("44")};
            STABLEARB_LOG_VERIFY(handler, (!price.error), "Price parse error");

            auto side = report.getNumber<unsigned int>("54");
            auto remaining = report.getDecimal<VolumeType>("151");
            auto lastRemaining = orders[orderId];

            auto* config = this->getConfig();
            auto tickSize = config->tickSize;

            // take-profit order
            auto executed = lastRemaining - remaining;
            if (executed > 0)
            {
                auto reversedSide = side == 1 ? 2 : 1;
                auto reversedPrice = side == 1 ? price + tickSize : price - tickSize;
                SingleQuote<Traits> quote{.price = reversedPrice, .volume = executed, .side = reversedSide};
                STABLEARB_LOG_INFO(
                    handler,
                    "Quoted take-profit",
                    (side == 2 ? "bid" : "ask"),
                    reversedPrice.template as<double>(),
                    '@',
                    quote.price.template as<double>());
            }

            auto it = orders.find(orderId);
            it->second = remaining;
            if (remaining.getValue() == 0)
                orders.erase(orderId);

            STABLEARB_LOG_INFO(
                handler,
                "Order filled:",
                orderId,
                "with remaining",
                remaining.template as<double>(),
                '@',
                price.template as<double>());
        }

        // cancelled
        if (status == 4)
        {
            orders.erase(orderId);
            STABLEARB_LOG_WARN(handler, "Order cancelled:", orderId);
        }

        // rejected
        if (status == 8)
        {
            auto reason = report.getNumber<unsigned int>("103");

            orders.erase(orderId);
            STABLEARB_LOG_ERROR(handler, "Order rejected:", orderId, "due to reason", reason);
        }
    }

    PriceType lastQuote;
    PriceType bestBid;
    PriceType bestAsk;
    VolumeType bestBidQty;
    VolumeType bestAskQty;

    // <order id, remaining volume>
    boost::unordered_flat_map<std::string, VolumeType> orders;
};

} // namespace stablearb
