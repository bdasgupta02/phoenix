#pragma once

#include "phoenix/common/logger.hpp"
#include "phoenix/data/fix.hpp"
#include "phoenix/data/order_book.hpp"
#include "phoenix/data/orders.hpp"
#include "phoenix/graph/router_handler.hpp"
#include "phoenix/tags.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace phoenix::triangular {

template<typename NodeBase>
struct Hitter : NodeBase
{
    using Router = NodeBase::Router;
    using Config = NodeBase::Config;
    using Traits = NodeBase::Traits;
    using Price = NodeBase::Traits::PriceType;
    using Volume = NodeBase::Traits::VolumeType;
    using PriceValue = NodeBase::Traits::PriceType::ValueType;
    using Order = SingleOrder<Traits>;
    using Book = OrderBook<Price, Volume>;

    Hitter(Config const& config, RouterHandler<Router>& handler)
        : NodeBase(config, handler)
        , config{&config}
        , handler{&handler}
        , maxVolume{this->config->volumeSize}
        , ethOrder{.symbol = config.instrumentList[0], .isLimit = true, .isFOK = true}
        , stethOrder{.symbol = config.instrumentList[1], .isLimit = true, .isFOK = true}
        , crossOrder{.symbol = config.instrumentList[2], .isLimit = false}
    {}

    void handle(tag::Hitter::MDSnapshot, FIXReaderFast& marketData)
    {
        auto symbol = marketData.getStringView(55);
        auto const index = getIndex(symbol);
        books[index].fromSnapshot(marketData);
    }

    [[gnu::hot, gnu::always_inline]]
    inline void handle(tag::Hitter::MDUpdate, FIXReaderFast& marketData)
    {
        auto symbol = marketData.getStringView(55);
        auto const index = getIndex(symbol);

        ///////// UPDATE PRICES
        books[index].fromUpdate(marketData);

        ///////// TRIGGER
        auto const& eth = books[0];
        auto const& steth = books[1];
        auto const& cross = books[2];

        auto ethBid = eth.getBestBid();
        auto ethAsk = eth.getBestAsk();

        auto stethBid = steth.getBestBid();
        auto stethAsk = steth.getBestAsk();

        auto crossBid = cross.getBestBid();
        auto crossAsk = cross.getBestAsk();
        
        /*if (index != 1u or fillMode) [[unlikely]]*/
        /*    return;*/

        // Case 1
        Volume volume = std::min({
            ethAsk.second,
            crossAsk.second / 2.0,
            stethBid.second,
            maxVolume});

        ethOrder.price = ethAsk.first;
        ethOrder.volume = volume * crossAsk.first.asDouble();
        ethOrder.side = 1;

        stethOrder.price = stethBid.first;
        stethOrder.volume = volume;
        stethOrder.side = 2;

        crossOrder.price = crossAsk.first;
        crossOrder.volume = volume;
        crossOrder.side = 1;

        if (execute(ethAsk.first * crossAsk.first < stethBid.first && !fillMode))
            PHOENIX_LOG_INFO(handler, "[OPP CASE 1] ETH", ethOrder.price.asDouble(), "* STETH/ETH", crossOrder.price.asDouble(), "< STETH", stethOrder.price.asDouble(), "- idx", index);

        // Case 2
        volume = std::min({
            ethBid.second,
            crossBid.second / 2.0,
            stethAsk.second,
            maxVolume});

        ethOrder.price = ethBid.first;
        ethOrder.volume = volume * crossBid.first.asDouble();
        ethOrder.side = 2;

        stethOrder.price = stethAsk.first;
        stethOrder.volume = volume;
        stethOrder.side = 1,

        crossOrder.price = crossBid.first;
        crossOrder.volume = volume;
        crossOrder.side = 2;

        if (execute(stethAsk.first < ethBid.first * crossBid.first && !fillMode))
            PHOENIX_LOG_INFO(handler, "[OPP CASE 2] ETH", ethOrder.price.asDouble(), "* STETH/ETH", crossOrder.price.asDouble(), "> STETH", stethOrder.price.asDouble(), "- idx", index);
    }

    inline void handle(tag::Hitter::ExecutionReport, FIXReaderFast& report)
    {
        auto symbol = report.getStringView(55);
        auto status = report.getNumber<unsigned>(39);
        auto orderId = report.getStringView(11);
        auto remaining = report.getDecimal<Volume>(151);
        auto justExecuted = report.getDecimal<Volume>(14);
        auto side = report.getNumber<unsigned>(54);
        auto price = report.getDecimal<Price>(44);

        switch (status)
        {
        case 0: 
        {
            logOrder("[NEW ORDER]", orderId, side, price, remaining); 
            auto& sentOrder = sentOrders[getIndex(symbol)];
            sentOrder.orderId = orderId;
            sentOrder.isInFlight = false;
        }
        break;

        case 1: logOrder("[PARTIAL FILL]", orderId, side, price, justExecuted); break;

        case 2:
        {
            auto const index = getIndex(symbol);
            if (index == 1u)
                handler->invoke(tag::Stream::TakeMarketOrders{}, sentOrders[0u], sentOrders[2u]);

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
            auto& sentOrder = sentOrders[getIndex(symbol)];

            if (sentOrder.price.asDouble() != avgFillPrice)
                PHOENIX_LOG_INFO(handler, "Slippage detected", sentOrder.price.asDouble(), avgFillPrice);

            sentOrder.isFilled = true;
            sentOrder.price = avgFillPrice;
            sentOrder.isInFlight = false;

            if (++filled == 3u)
            {
                fillMode = false;
                filled = 0u;
                fillRetried = false;
                PHOENIX_LOG_INFO(handler, "All orders filled");
                updatePnl();
            }
        }
        break;

        case 4: 
        {
            logOrder("[CANCELLED]", orderId, side, price, remaining);
            auto const index = getIndex(symbol);
            auto& sentOrder = sentOrders[index];

            if (index == 1u)
            {
                fillMode = false;
                filled = 0u;
                fillRetried = false;
            }
            else
            {
                /*if (sentOrder.side == 1)*/
                /*    sentOrder.price = books[index].getBestBid().first;*/
                /*else*/
                /*    sentOrder.price = books[index].getBestAsk().first;*/
                sentOrder.isLimit = false;
                while (!handler->retrieve(tag::Stream::TakeMarketOrders{}, true, sentOrder));
                sentOrder.lastSent = std::chrono::steady_clock::now();
                PHOENIX_LOG_INFO(handler, "Retrying", symbol);
            }
        }
        break;

        case 8:
        {
            auto reason = report.getStringView(103);
            logOrder("[REJECTED]", orderId, side, price, remaining, reason);
        }
        break;

        default: PHOENIX_LOG_WARN(handler, "Other status type", status); break;
        };
    }

    inline void handle(tag::Hitter::InitBalances) {}

private:
    inline void updatePnl()
    {
        auto& steth = sentOrders[1];
        double stethPrice = steth.price.asDouble();
        double ethPrice = sentOrders[0].price.asDouble();
        double crossPrice = sentOrders[2].price.asDouble();

        double const contractSize = config->contractSize;
        double const multiplier = steth.volume.asDouble() * contractSize;

        if (sentOrders[0].side == 1)
            pnl += (stethPrice - (ethPrice * crossPrice)) * multiplier;
        else
            pnl += ((ethPrice * crossPrice) - stethPrice) * multiplier;

        PHOENIX_LOG_INFO(handler, "[PNL]", pnl, " in USD (estimate)");
    }

    inline Order& getOrderBySymbol(std::string const& symbol)
    {
        auto const it = config->instrumentMap.find(symbol);
        return sentOrders[it->second];
    }

    inline void logOrder(
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

    [[gnu::hot, gnu::always_inline]]
    inline bool execute(bool const take)
    {
        if (handler->retrieve(tag::Stream::SendQuotes{}, take, stethOrder))
        {
            sentOrders[0] = ethOrder;
            sentOrders[1] = stethOrder;
            sentOrders[2] = crossOrder;
            fillMode = true;
            filled = 0u;
            PHOENIX_LOG_INFO(handler, "Attemping to take STETH");
        }

        return take;
    }

    [[gnu::hot, gnu::always_inline]]
    inline std::size_t getIndex(std::string_view symbol) const
    {
        switch (symbol[6u])
        {
            // ETH_US D C
            case 'D': return 0u;
            // STETH_ U SDC
            case 'U': return 1u;
            // STETH_ E TH
            default: return 2u;
        }
    }

    RouterHandler<Router>* const handler;
    Config const* const config;
    Volume const maxVolume;

    std::array<Book, 3u> books;
    std::array<Order, 3u> sentOrders;

    bool fillMode = false;
    bool fillRetried = false;
    unsigned filled = 0u;

    double pnl = 0.0;
    static constexpr std::chrono::milliseconds SEND_INTERVAL_INITIAL{40000u};
    static constexpr std::chrono::milliseconds SEND_INTERVAL_RETRY{10000u};

    Order ethOrder;
    Order stethOrder;
    Order crossOrder;

    std::size_t stethCounter = 0u;
};

/*template<typename NodeBase>*/
/*struct Hitter : NodeBase*/
/*{*/
/*    using Router = NodeBase::Router;*/
/*    using Config = NodeBase::Config;*/
/*    using Traits = NodeBase::Traits;*/
/*    using Price = NodeBase::Traits::PriceType;*/
/*    using Volume = NodeBase::Traits::VolumeType;*/
/*    using PriceValue = NodeBase::Traits::PriceType::ValueType;*/
/*    using Order = SingleOrder<Traits>;*/
/**/
/*    Hitter(Config const& config, RouterHandler<Router>& handler)*/
/*        : NodeBase(config, handler)*/
/*        , config{&config}*/
/*        , handler{&handler}*/
/*    {}*/
/**/
/*    [[gnu::hot, gnu::always_inline]]*/
/*    inline void handle(tag::Hitter::MDUpdate, FIXReaderFast& marketData, bool const update = true)*/
/*    {*/
/*        auto const& instrumentMap = config->instrumentMap;*/
/*        auto symbol = marketData.getStringView(55);*/
/*        auto const index = getIndex(symbol);*/
/**/
/*        ///////// UPDATE PRICES*/
/*        Price newBid;*/
/*        Price newAsk;*/
/*        Volume newBidQty;*/
/*        Volume newAskQty;*/
/*        std::size_t const numUpdates = marketData.getNumber<std::size_t>(268);*/
/*        for (std::size_t i = 0u; i < numUpdates; ++i)*/
/*        {*/
/*            unsigned const typeField = marketData.getNumber<unsigned>(269, i);*/
/*            if (typeField == 0u)*/
/*            {*/
/*                newBid.minOrZero(marketData.getDecimal<Price>(270, i));*/
/*                newBidQty.minOrZero(marketData.getDecimal<Volume>(271, i));*/
/*            }*/
/*            if (typeField == 1u)*/
/*            {*/
/*                newAsk.minOrZero(marketData.getDecimal<Price>(270, i));*/
/*                newAskQty.minOrZero(marketData.getDecimal<Volume>(271, i));*/
/*            }*/
/*        }*/
/**/
/*        auto& instrumentPrices = bestPrices[index];*/
/*        instrumentPrices.bid = newBid;*/
/*        instrumentPrices.bidQty = newBidQty;*/
/*        instrumentPrices.ask = newAsk;*/
/*        instrumentPrices.askQty = newAskQty;*/
/**/
/*        ///////// TRIGGER*/
/*        auto& eth = bestPrices[0];*/
/*        auto& steth = bestPrices[1];*/
/*        auto& cross = bestPrices[2];*/
/**/
/*        if (index != 1u || !update || fillMode) [[unlikely]]*/
/*            return;*/
/**/
/*        Volume const maxVolume{config->volumeSize};*/
/**/
/*        // Buy ETH, Sell STETH, Buy STETH/ETH*/
/*        if (eth.ask * cross.ask < steth.bid)*/
/*        {*/
/*            Volume const volume = std::min({eth.askQty, cross.askQty, steth.bidQty, maxVolume});*/
/**/
/*            // clang-format off*/
/*            Order buyEth{*/
/*                .symbol = config->instrumentList[0],*/
/*                .price = eth.ask,*/
/*                .volume = volume * cross.ask.asDouble(),*/
/*                .side = 1,*/
/*                .isLimit = false,*/
/*            };*/
/**/
/*            Order sellSteth{*/
/*                .symbol = config->instrumentList[1],*/
/*                .price = steth.bid,*/
/*                .volume = volume,*/
/*                .side = 2,*/
/*                .isLimit = false,*/
/*            };*/
/**/
/*            Order buyCross{*/
/*                .symbol = config->instrumentList[2],*/
/*                .price = cross.ask,*/
/*                .volume = volume,*/
/*                .side = 1,*/
/*                .isLimit = false,*/
/*            };*/
/*            // clang-format on*/
/**/
/*            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, sellSteth, buyEth, buyCross))*/
/*            {*/
/*                sentOrders[0] = buyEth;*/
/*                sentOrders[1] = sellSteth;*/
/*                sentOrders[2] = buyCross;*/
/*                fillMode = true;*/
/*                filled = 0u;*/
/*            }*/
/**/
/*            PHOENIX_LOG_INFO(handler, "[OPP CASE 1] ETH", eth.ask.asDouble(), "* STETH/ETH", cross.ask.asDouble(), "< STETH", steth.bid.asDouble());*/
/*        }*/
/**/
/*        // Sell ETH, Buy STETH, Sell STETH/ETH*/
/*        if (steth.ask < eth.bid * cross.bid)*/
/*        {*/
/*            Volume const volume = std::min({steth.askQty, eth.bidQty, cross.bidQty, maxVolume});*/
/**/
/*            // clang-format off*/
/*            Order sellEth{*/
/*                .symbol = config->instrumentList[0],*/
/*                .price = eth.bid,*/
/*                .volume = volume * cross.bid.asDouble(),*/
/*                .side = 2,*/
/*                .isLimit = false,*/
/*            };*/
/**/
/*            Order buySteth{*/
/*                .symbol = config->instrumentList[1],*/
/*                .price = steth.ask,*/
/*                .volume = volume,*/
/*                .side = 1,*/
/*                .isLimit = false,*/
/*            };*/
/**/
/*            Order sellCross{*/
/*                .symbol = config->instrumentList[2],*/
/*                .price = cross.bid,*/
/*                .volume = volume,*/
/*                .side = 2,*/
/*                .isLimit = false,*/
/*            };*/
/*            // clang-format on*/
/**/
/*            if (handler->retrieve(tag::Stream::TakeMarketOrders{}, buySteth, sellEth, sellCross))*/
/*            {*/
/*                sentOrders[0] = sellEth;*/
/*                sentOrders[1] = buySteth;*/
/*                sentOrders[2] = sellCross;*/
/*                fillMode = true;*/
/*                filled = 0u;*/
/*            }*/
/**/
/*            PHOENIX_LOG_INFO(handler, "[OPP CASE 2] ETH", eth.bid.asDouble(), "* STETH/ETH", cross.bid.asDouble(), "> STETH", steth.ask.asDouble());*/
/*        }*/
/*    }*/
/**/
/*    [[gnu::hot, gnu::always_inline]]*/
/*    inline void handle(tag::Hitter::ExecutionReport, FIXReaderFast& report)*/
/*    {*/
/*        auto symbol = report.getStringView(55);*/
/*        auto status = report.getNumber<unsigned>(39);*/
/*        auto orderId = report.getStringView(11);*/
/*        auto remaining = report.getDecimal<Volume>(151);*/
/*        auto justExecuted = report.getDecimal<Volume>(14);*/
/*        auto side = report.getNumber<unsigned>(54);*/
/*        auto price = report.getDecimal<Price>(44);*/
/**/
/*        switch (status)*/
/*        {*/
/*        case 0: */
/*        {*/
/*            auto const it = config->instrumentMap.find(symbol);*/
/*            auto& sentOrder = sentOrders[it->second];*/
/*            logOrder("[NEW ORDER]", orderId, side, price, remaining); */
/*            sentOrder.orderId = orderId;*/
/*            sentOrder.isInFlight = false;*/
/*        }*/
/*        break;*/
/**/
/*        case 1: */
/*        {*/
/*            unsigned const numFills = report.getNumber<unsigned>(1362);*/
/*            double lastFillQty = 0.0;*/
/*            for (unsigned i = 0u; i < numFills; ++i)*/
/*                lastFillQty = report.getNumber<double>(1365, i);*/
/**/
/*            logOrder("[PARTIAL FILL]", orderId, side, price, lastFillQty);*/
/*            auto const it = config->instrumentMap.find(symbol);*/
/*            auto& sentOrder = sentOrders[it->second];*/
/*            sentOrder.volume -= lastFillQty;*/
/*            sentOrder.orderId = orderId;*/
/*            sentOrder.isInFlight = false;*/
/*        }*/
/*        break;*/
/**/
/*        case 2:*/
/*        {*/
/*            unsigned const numFills = report.getNumber<unsigned>(1362);*/
/*            double avgFillPrice = 0.0;*/
/*            double totalQty = 0.0;*/
/*            for (unsigned i = 0u; i < numFills; ++i)*/
/*            {*/
/*                double const fillQty = report.getNumber<double>(1365, i);*/
/*                double const fillPrice = report.getNumber<double>(1364, i);*/
/*                totalQty += fillQty;*/
/*                avgFillPrice += (fillQty * fillPrice);*/
/*            }*/
/*            if (totalQty && avgFillPrice)*/
/*                avgFillPrice /= totalQty;*/
/**/
/*            logOrder("[FILL]", orderId, side, avgFillPrice, justExecuted);*/
/*            auto const it = config->instrumentMap.find(symbol);*/
/*            auto& sentOrder = sentOrders[it->second];*/
/**/
/*            if (avgFillPrice != sentOrder.price)*/
/*                PHOENIX_LOG_WARN(handler, "Slippage detected", price.asDouble(), sentOrder.price.asDouble());*/
/**/
/*            sentOrder.isFilled = true;*/
/*            sentOrder.price = avgFillPrice;*/
/*            sentOrder.isInFlight = false;*/
/**/
/*            if (++filled == 3u)*/
/*            {*/
/*                fillMode = false;*/
/*                filled = 0u;*/
/*                fillRetried = false;*/
/*                PHOENIX_LOG_INFO(handler, "All orders filled");*/
/*                updatePnl();*/
/*            }*/
/*        }*/
/*        break;*/
/**/
/*        case 4: */
/*        {*/
/*            logOrder("[CANCELLED]", orderId, side, price, remaining);*/
/*            auto const it = config->instrumentMap.find(symbol);*/
/*            auto& sentOrder = sentOrders[it->second];*/
/*            if (sentOrder.side == 1)*/
/*                sentOrder.price = bestPrices[it->second].bid;*/
/*            else*/
/*                sentOrder.price = bestPrices[it->second].ask;*/
/**/
/*            while (!handler->retrieve(tag::Stream::TakeMarketOrders{}, sentOrder));*/
/**/
/*            sentOrder.lastSent = std::chrono::steady_clock::now();*/
/*            sentOrder.isInFlight = true;*/
/*            PHOENIX_LOG_INFO(handler, "Retrying", symbol);*/
/*        }*/
/*        break;*/
/**/
/*        case 8:*/
/*        {*/
/*            auto reason = report.getStringView(103);*/
/*            logOrder("[REJECTED]", orderId, side, price, remaining, reason);*/
/*        }*/
/*        break;*/
/**/
/*        default: PHOENIX_LOG_WARN(handler, "Other status type", status); break;*/
/*        }*/
/*    }*/
/**/
/*    inline void handle(tag::Hitter::InitBalances) {}*/
/**/
/*private:*/
/*    inline void updatePnl()*/
/*    {*/
/*        auto& steth = sentOrders[1];*/
/*        double stethPrice = steth.price.asDouble();*/
/*        double ethPrice = sentOrders[0].price.asDouble();*/
/*        double crossPrice = sentOrders[2].price.asDouble();*/
/**/
/*        double const contractSize = config->contractSize;*/
/*        double const multiplier = steth.volume.asDouble() * contractSize;*/
/**/
/*        if (sentOrders[0].side == 1)*/
/*            pnl += (stethPrice - (ethPrice * crossPrice)) * multiplier;*/
/*        else*/
/*            pnl += ((ethPrice * crossPrice) - stethPrice) * multiplier;*/
/**/
/*        PHOENIX_LOG_INFO(handler, "[PNL]", pnl, " in USD (estimate)");*/
/*    }*/
/**/
/*    inline Order& getOrderBySymbol(std::string const& symbol)*/
/*    {*/
/*        auto const it = config->instrumentMap.find(symbol);*/
/*        return sentOrders[it->second];*/
/*    }*/
/**/
/*    inline void logOrder(*/
/*        std::string_view type,*/
/*        std::string_view orderId,*/
/*        unsigned side,*/
/*        Price price,*/
/*        Volume volume,*/
/*        std::string_view rejectReason = "")*/
/*    {*/
/*        PHOENIX_LOG_INFO(*/
/*            handler,*/
/*            type,*/
/*            orderId,*/
/*            side == 1 ? "BUY" : "SELL",*/
/*            volume.asDouble() * 0.0001,*/
/*            '@',*/
/*            price.asDouble(),*/
/*            rejectReason.empty() ? "" : "with reason",*/
/*            rejectReason);*/
/*    }*/
/**/
/*    struct InstrumentTopLevel*/
/*    {*/
/*        Price bid{PriceValue{}};*/
/*        Price ask{std::numeric_limits<PriceValue>::max()};*/
/*        Volume bidQty{};*/
/*        Volume askQty{};*/
/*    };*/
/**/
/*    RouterHandler<Router>* const handler;*/
/*    Config const* const config;*/
/**/
/*    std::array<InstrumentTopLevel, 3u> bestPrices;*/
/*    std::array<Order, 3u> sentOrders;*/
/**/
/*    bool fillMode = false;*/
/*    bool fillRetried = false;*/
/*    unsigned filled = 0u;*/
/**/
/*    double pnl = 0.0;*/
/*    static constexpr std::chrono::milliseconds SEND_INTERVAL_INITIAL{30u * 60u * 1000u};*/
/*    static constexpr std::chrono::milliseconds SEND_INTERVAL_RETRY{10000u};*/
/**/
/*    std::size_t stethCounter = 0u;*/
/*};*/

} // namespace phoenix::triangular
