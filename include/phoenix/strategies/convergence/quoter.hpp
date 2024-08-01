#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/quotes.hpp"

#include <boost/container/flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace phoenix {

template<typename NodeBase>
struct Quoter : NodeBase
{
    using NodeBase::NodeBase;

    using Traits = NodeBase::Traits;
    using PriceType = NodeBase::Traits::PriceType;
    using VolumeType = NodeBase::Traits::VolumeType;

    using PriceValue = PriceType::ValueType;
    using Bids = boost::container::flat_set<PriceValue, std::greater<PriceValue>>;
    using Asks = boost::container::flat_set<PriceValue, std::less<PriceValue>>;

    // TODO: handle case where only one side is available - now it leads to fatal
    inline void handle(tag::Quoter::Quote, FIXReader&& topLevel, std::size_t seqNum)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        std::int64_t bidIdx = -1u;
        std::int64_t askIdx = -1u;

        PriceType const lastBid = bestBid;
        PriceType const lastAsk = bestAsk;

        std::size_t const numUpdates = topLevel.getFieldSize("269");
        for (std::int64_t i = 0; i < numUpdates; ++i)
        {
            auto typeField = topLevel.getNumber<unsigned int>("269", i);

            if (typeField == 0)
                bidIdx = i;

            if (typeField == 1)
                askIdx = i;
        }

        if (bidIdx > -1)
        {
            bestBid = topLevel.getDecimal<PriceType>("270", bidIdx);
            PHOENIX_LOG_VERIFY(handler, (!bestBid.error), "Decimal parse error");
        }

        if (askIdx > -1)
        {
            bestAsk = topLevel.getDecimal<PriceType>("270", askIdx);
            PHOENIX_LOG_VERIFY(handler, (!bestAsk.error), "Decimal parse error");
        }

        updateIndex(topLevel);

        for (auto price : bidsQuoted)
        {
            if (bestBid - config->quoteResetThreshold > price)
            {
                handler->invoke(tag::Stream::CancelQuote{}, quotedLevels[price]);
                PHOENIX_LOG_INFO(
                    handler,
                    "[RESET]",
                    "BID",
                    PriceType{price}.template as<double>(),
                    "with best bid",
                    bestBid.template as<double>());
            }
            else
                break;
        }

        for (auto price : asksQuoted)
        {
            if (bestAsk + config->quoteResetThreshold < price)
            {
                handler->invoke(tag::Stream::CancelQuote{}, quotedLevels[price]);
                PHOENIX_LOG_INFO(
                    handler,
                    "[RESET]",
                    "ASK",
                    PriceType{price}.template as<double>(),
                    "with best ask",
                    bestAsk.template as<double>());
            }
            else
                break;
        }

        PriceType const tickSize = config->tickSize;
        VolumeType const lotSize = config->lotSize;
        VolumeType const doubleLotSize = lotSize + lotSize;
        bool const aggressive = config->aggressive;

        // > lotSize to prevent trading on my own book event
        if (bestBid < 1.0 && lastBid != bestBid && !quotedLevels.contains(bestBid.getValue()))
        {
            if (aggressive)
            {
                PriceType const aggressiveBid = bestBid + tickSize;
                if (aggressiveBid < 1.0 && aggressiveBid < bestAsk && !quotedLevels.contains(aggressiveBid.getValue()))
                {
                    sendQuote({.price = aggressiveBid, .volume = lotSize, .side = 1});
                    sendQuote({.price = bestBid, .volume = doubleLotSize, .side = 1});
                }
                else
                    sendQuote({.price = bestBid, .volume = doubleLotSize, .side = 1});
            }
            else
                sendQuote({.price = bestBid, .volume = lotSize, .side = 1});
        }

        if (bestAsk > 1.0 && lastAsk != bestAsk && !quotedLevels.contains(bestAsk.getValue()))
        {
            if (aggressive)
            {
                PriceType const aggressiveAsk = bestAsk - tickSize;
                if (aggressiveAsk > 1.0 && aggressiveAsk > bestBid && !quotedLevels.contains(aggressiveAsk.getValue()))
                {
                    sendQuote({.price = aggressiveAsk, .volume = lotSize, .side = 2});
                    sendQuote({.price = bestAsk, .volume = doubleLotSize, .side = 2});
                }
                else
                    sendQuote({.price = bestAsk, .volume = doubleLotSize, .side = 2});
            }
            else
                sendQuote({.price = bestAsk, .volume = lotSize, .side = 2});
        }
    }

    inline void handle(tag::Quoter::ExecutionReport, FIXReader&& report)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        auto status = report.getNumber<unsigned int>("39");
        auto orderId = report.getString("11");
        auto clOrderId = report.getString("41");
        auto remaining = report.getDecimal<VolumeType>("151");
        auto justExecuted = report.getDecimal<VolumeType>("14");
        auto side = report.getNumber<unsigned int>("54");
        auto price = report.getDecimal<PriceType>("44");
        PHOENIX_LOG_VERIFY(handler, (!price.error && !remaining.error), "Decimal parse error");

        // new order
        if (status == 0)
        {
            logOrder("[NEW ORDER]", orderId, clOrderId, side, price, remaining);
            auto priceValue = price.getValue();

            orders[orderId] = remaining;
            quotedLevels[priceValue] = orderId;

            if (side == 1)
                bidsQuoted.insert(priceValue);
            else
                asksQuoted.insert(priceValue);

            handler->invoke(tag::Risk::UpdatePosition{}, remaining.template as<double>(), side);
        }

        // partial/total fill
        if (status == 1 || status == 2)
        {
            logOrder("[FILL]", orderId, clOrderId, side, price, justExecuted);
            auto tickSize = config->tickSize;
            auto lastRemaining = orders[orderId];
            auto executed = lastRemaining - remaining;

            if (clOrderId.size() > 0 && clOrderId[0] == 't')
            {
                takeProfitFilled += justExecuted.getValue();
                baseFilled += justExecuted.getValue();
            }
            else if (0u < executed)
            {
                unsigned int reversedSide = side == 1 ? 2 : 1;
                PriceType reversedPrice = side == 1 ? price + tickSize : price - tickSize;
                sendQuote({.price = reversedPrice, .volume = executed, .side = reversedSide, .takeProfit = true});
            }

            PHOENIX_LOG_INFO(handler, "[EDGE CAPTURED]", takeProfitFilled);
            PHOENIX_LOG_INFO(handler, "[EXPOSURE]", baseFilled);

            if (remaining.getValue() == 0)
            {
                orders.erase(orderId);
                quotedLevels.erase(price.getValue());
                if (side == 1)
                    bidsQuoted.erase(price.getValue());
                else
                    asksQuoted.erase(price.getValue());
            }
            else
                orders[orderId] = remaining;
        }

        // cancelled
        if (status == 4)
        {
            logOrder("[CANCELLED]", orderId, clOrderId, side, price, remaining);
            orders.erase(orderId);
            quotedLevels.erase(price.getValue());
            if (side == 1)
                bidsQuoted.erase(price.getValue());
            else
                asksQuoted.erase(price.getValue());
            handler->invoke(tag::Risk::UpdatePosition{}, -remaining.template as<double>(), side);
        }

        // rejected
        if (status == 8)
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, clOrderId, side, price, remaining, reason);
            orders.erase(orderId);
            quotedLevels.erase(price.getValue());
            if (side == 1)
                bidsQuoted.erase(price.getValue());
            else
                asksQuoted.erase(price.getValue());
            handler->invoke(tag::Risk::UpdatePosition{}, -remaining.template as<double>(), side);
        }
    }

private:
    void logOrder(
        std::string_view type,
        std::string_view orderId,
        std::string_view clOrderId,
        unsigned int side,
        PriceType price,
        VolumeType volume,
        std::string_view rejectReason = "")
    {
        auto* handler = this->getHandler();
        PHOENIX_LOG_INFO(
            handler,
            type,
            orderId,
            clOrderId,
            side == 1 ? "BID" : "ASK",
            volume.template as<double>(),
            '@',
            price.template as<double>(),
            rejectReason.empty() ? "" : "with reason",
            rejectReason);
    }

    void sendQuote(SingleQuote<Traits> quote)
    {
        auto* handler = this->getHandler();

        if (!handler->retrieve(tag::Risk::Check{}, quote))
            return;

        handler->invoke(tag::Stream::SendQuote{}, quote);
        PHOENIX_LOG_INFO(
            handler,
            "[QUOTED]",
            quote.takeProfit ? "[TAKE PROFIT]" : "",
            quote.side == 1 ? "BID" : "ASK",
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
            PHOENIX_LOG_INFO(this->getHandler(), "Index price changed to", index.template as<double>());
        }
    }

    PriceType bestBid;
    PriceType bestAsk;
    PriceType index;

    // <order id, remaining volume>
    boost::unordered_flat_map<std::string, VolumeType> orders;

    // <level price, order id>
    boost::unordered_flat_map<PriceValue, std::string> quotedLevels;

    Bids bidsQuoted;
    Asks asksQuoted;

    std::uint64_t takeProfitFilled = 0u;
    std::uint64_t baseFilled = 0u;
};

} // namespace phoenix
