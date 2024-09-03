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
// if theo falls/rises abruptly, pickoff stale entries with FOK
// if FOK fills, place opposing side at theo
// if theo recovers (with threshold / 2), take market order to exit inventory

// Made specifically for BTC/USDC for now

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
    inline void handle(tag::Hitter::MDUpdate, FIXReader& marketData)
    {
        ///////// UPDATE PRICES

        Price newBid;
        Price newAsk;

        for (std::size_t i = 0u; i < 2u; ++i)
        {
            unsigned const typeField = marketData.getNumber<unsigned>("269", i);
            if (typeField == 0u)
                newBid.minOrZero(marketData.getDecimal<Price>("270", i));
            if (typeField == 1u)
                newAsk.minOrZero(marketData.getDecimal<Price>("270", i));
        }

        Price const newIndex = marketData.getDecimal<Price>("100090");

        PHOENIX_LOG_VERIFY(handler, (newBid && newAsk && newIndex), "Missing prices in MD");

        ///////// EXITING POSITION
        // simply tries to join best level for now

        if (fillMode)
        {
            // adding some stickiness to capture orders due to extreme jitter in prices
            Price const diffAsk = newAsk > sentAsk.price ? newAsk - sentAsk.price : sentAsk.price - newAsk;
            Price const diffBid = newBid > sentBid.price ? newBid - sentBid.price : sentBid.price - newBid;

            // ask quote to capture spread isn't the top level
            if (bidSniped && diffAsk > 15.0)
            {
                Order capture{.price=newAsk + 5.0, .volume=1.0, .side=2, .takeProfit=true};
                handler->retrieve(tag::Stream::SendQuotes{}, capture);
                sentAsk = capture;
                PHOENIX_LOG_INFO(handler, "Readjusting ask for capture", newBid.asDouble());
            }

            // bid quote to capture spread isn't the top level
            else if (!bidSniped && diffBid > 15.0)
            {
                Order capture{.price=newBid - 5.0, .volume=1.0, .side=1, .takeProfit=true};
                handler->retrieve(tag::Stream::SendQuotes{}, capture);
                sentBid = capture;
                PHOENIX_LOG_INFO(handler, "Readjusting bid for capture", newBid.asDouble());
            }

            return;
        }

        ///////// TRIGGER
        
        if (isInflight)
            return;

        Price const tickSize = config->tickSize;

        if (newIndex < newBid && newIndex < lastIndex - THRESHOLD)
        {
            Order pickoff{.price=newBid, .volume=1.0, .side=2, .isFOK=true};
            if (handler->retrieve(tag::Stream::SendQuotes{}, pickoff))
            {
                sentAsk = pickoff;
                triggeredIndex = newIndex;
                isInflight = true;
                PHOENIX_LOG_INFO(handler, "Picking off stale bid", newBid.asDouble(), "with index", newIndex.asDouble());
            }
        }
        else if (newIndex > newAsk && newIndex > lastIndex + THRESHOLD)
        {
            Order pickoff{.price=newAsk, .volume=1.0, .side=1, .isFOK=true};
            if (handler->retrieve(tag::Stream::SendQuotes{}, pickoff))
            {
                sentBid = pickoff;
                triggeredIndex = newIndex;
                isInflight = true;
                PHOENIX_LOG_INFO(handler, "Picking off stale ask", newAsk.asDouble(), "with index", newIndex.asDouble());
            }
        }

        lastBid = newBid;
        lastAsk = newAsk;
        lastIndex = newIndex;
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

        bool const isCaptureOrder = clOrderId.size() > 0u && clOrderId[0] == 't';
        unsigned const reversedSide = side == 1 ? 2 : 1;

        // clang-format off
        switch (status)
        {
        case 1:
            logOrder("[PARTIAL FILL]", clOrderId, side, price, remaining);
            break;
        case 0:
            logOrder("[NEW ORDER]", clOrderId, side, price, remaining);
            break;
        case 8:
            PHOENIX_LOG_FATAL(handler, "Order rejected with id", clOrderId, "and reason", report.getStringView("103"));
            break;

        case 4: 
        {
            logOrder("[CANCELLED]", clOrderId, side, price, remaining);
            if (!isCaptureOrder)
                isInflight = false;
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

            logOrder("[FILL]", clOrderId, side, avgFillPrice, remaining);
            
            if (isCaptureOrder)
            {
                pnl += sentAsk.price.asDouble() - sentBid.price.asDouble();
                PHOENIX_LOG_INFO(handler, "Current PNL (in qty):", pnl);
                fillMode = false;
            }
            else 
            {
                Order capture{
                    .price=reversedSide == 1 ? lastBid - 15.0 : lastAsk + 15.0,
                    .volume=1.0,
                    .side=reversedSide,
                    .takeProfit=true
                };

                while (!handler->retrieve(tag::Stream::SendQuotes{}, capture))
                    _mm_pause();

                isInflight = false;
                fillMode = true;
                bidSniped = side == 1;
                if (side == 1)
                    sentAsk = capture;
                else 
                    sentBid = capture;
            }
        }
        break;

        default:
            PHOENIX_LOG_WARN(handler, "Other status type", status);
            break;
        };
        // clang-format on
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

    RouterHandler<Router>* const handler;
    Config const* const config;

    // best prices
    Price lastBid;
    Price lastAsk;
    Price lastIndex;

    // fill mode
    bool fillMode = false;
    bool isInflight = false;
    bool bidSniped;

    // trigger 
    static constexpr Price THRESHOLD{30.0};
    Order sentBid;
    Order sentAsk;
    Price triggeredIndex;

    // analysis
    double pnl = 0.0;
};

} // namespace phoenix::sniper
