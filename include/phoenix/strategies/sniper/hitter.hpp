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
// if theo recovers (with threshold), take market order to exit inventory

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

        ///////// EXITING POSITION

        if (fillMode)
        {
            return;
        }

        ///////// TRIGGER

        if (!update || !bestIndex)
        {
            PHOENIX_LOG_WARN(handler, "Update or index invalid");
            return;
        }

        Price const tickSize = config->tickSize;
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

        }
        break;

        case 8:
        {
            auto reason = report.getStringView("103");
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
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

    RouterHandler<Router>* const handler;
    Config const* const config;

    // best prices
    Price bestBid;
    Price bestAsk;
    Price bestIndex;

    // fill mode
    bool fillMode = false;
    Order sentBid;
    Order sentAsk;

    // analysis
    double pnl = 0.0;
};

} // namespace phoenix::sniper
