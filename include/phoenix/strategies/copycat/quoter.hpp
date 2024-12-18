#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/order_book.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/graph/router_handler.hpp"
#include "phoenix/tags.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace phoenix::copycat {

template<typename NodeBase>
struct Quoter : NodeBase
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

    void handle(tag::Quoter::MDUpdate, FIXReaderFast& marketData)
    {
        ///////// UPDATE PRICES
        auto symbol = marketData.getStringView(55);
        std::size_t const numUpdates = marketData.getNumber<std::size_t>(268);
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>(269, i);
            if (typeField == 0u)
                bid.minOrZero(marketData.getDecimal<Price>(270, i));
            if (typeField == 1u)
                ask.minOrZero(marketData.getDecimal<Price>(270, i));
        }

        Volume const volume{config->volumeSize};
        theo = (bid + ask) / 2.0;

        ///////// TRIGGER
        if (fillMode) [[unlikely]]
            return;

        if (allocated)
        {
            if (bid != sentBid.price && !sentBid.isInFlight)
            {
                Order newBid = sentBid;
                newBid.price = bid;

                if (handler->retrieve(tag::Stream::ModifyQuote{}, newBid))
                    sentBid = newBid;
            }

            if (ask != sentAsk.price && !sentAsk.isInFlight)
            {
                Order newAsk = sentAsk;
                newAsk.price = ask;

                if (handler->retrieve(tag::Stream::ModifyQuote{}, newAsk))
                {
                    sentAsk = newAsk;
                }
            }

            return;
        }

        Order bidOrder{
            .symbol = symbol,
            .price = bid,
            .volume = volume,
            .side = 1,
            .isInFlight = true,
        };

        Order askOrder{
            .symbol = symbol,
            .price = bid,
            .volume = volume,
            .side = 1,
            .isInFlight = true,
        };

        if (handler->retrieve(tag::Stream::SendQuotes{}, bidOrder, askOrder))
        {
            allocated = true;
            PHOENIX_LOG_INFO(handler, "Quotes allocated");
        }
    }

    void handle(tag::Quoter::ExecutionReport, FIXReaderFast& report)
    {
        auto status = report.getNumber<unsigned>(39);
        auto orderId = report.getStringView(11);
        auto remaining = report.getDecimal<Volume>(151);
        auto justExecuted = report.getDecimal<Volume>(14);
        auto side = report.getNumber<unsigned>(54);
        auto price = report.getDecimal<Price>(44);
        auto& sentOrder = side == 1 ? sentBid : sentAsk;

        switch (status)
        {
        case 0: 
        {
            logOrder("[NEW ORDER]", orderId, side, price, remaining); 
            sentOrder.orderId = orderId;
            sentOrder.isInFlight = false;
        }
        break;

        case 1:
        {
            logOrder("[PARTIAL FILL]", orderId, side, price, justExecuted);
        }
        break;

        case 2:
        {
            unsigned const numFills = report.getNumber<unsigned>(1362);
            double avgFillPrice = 0.0;
            double totalQty = 0.0;
            for (unsigned i = 0u; i < numFills; ++i)
            {
                double const fillQty = report.getNumber<double>(1365, i);
                double const fillPrice = report.getNumber<double>(1364, i);
                totalQty += fillQty;
                avgFillPrice += (fillQty * fillPrice);
            }
            if (totalQty && avgFillPrice)
                avgFillPrice /= totalQty;

            logOrder("[FILL]", orderId, side, avgFillPrice, justExecuted);

            fillMode = true;
            sentOrder.isFilled = true;
            sentOrder.price = avgFillPrice;
            sentOrder.isInFlight = false;

            if (++filled == 2u)
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

            if (it->second != 2u)
            {
                if (sentOrder.side == 1)
                    sentOrder.price += 0.1;
                else
                    sentOrder.price -= 0.1;
            }
            else 
            {
                if (sentOrder.side == 1)
                    sentOrder.price = bestPrices[it->second].bid;
                else
                    sentOrder.price = bestPrices[it->second].ask;
            }

            while (!handler->retrieve(tag::Stream::TakeMarketOrders{}, sentOrder));

            sentOrder.lastSent = std::chrono::steady_clock::now();
            sentOrder.isInFlight = false;
            PHOENIX_LOG_INFO(handler, "Retrying", symbol);
        }
        break;

        case 8:
        {
            auto reason = report.getStringView(103);
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
        }
        break;

        default: PHOENIX_LOG_WARN(handler, "Other status type", status); break;
        }
    }

private:
    void logOrder(
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
            volume.asDouble() * 0.0001,
            '@',
            price.asDouble(),
            rejectReason.empty() ? "" : "with reason",
            rejectReason);
    }

    RouterHandler<Router>* const handler;
    Config const* const config;

    Price theo;
    Price bid;
    Price ask;

    Order sentBid;
    Order sentAsk;
    
    bool allocated = false;
    bool fillMode = false;
    unsigned fills = 0u;
}

}
