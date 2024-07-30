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

        if (topLevel.getNumber<unsigned int>("269", 0) == 0)
        {
            bestBid = topLevel.getDecimal<PriceType>("270", 0);
            bestAsk = topLevel.getDecimal<PriceType>("270", 1);
        }
        else if (topLevel.getNumber<unsigned int>("269", 1) == 0)
        {
            bestBid = topLevel.getDecimal<PriceType>("270", 1);
            bestAsk = topLevel.getDecimal<PriceType>("269", 0);
        }
        else
        {
            STABLEARB_LOG_ERROR(handler, "Invalid top level market data: cannot find bid");
            return;
        }

        if (bestBid < 1.0 && lastBid != bestBid)
        {
            SingleQuote<Traits> quote{.price = bestBid, .volume = config->lotSize, .side = 1};

            if (!handler->invoke(tag::Risk::Check, quote))
                return;

            handler->invoke(tag::Stream::SendQuote{}, quote);
            STABLEARB_LOG_INFO(handler, "Quoted bid side with price", bestBid.template as<double>());
        }

        if (bestAsk > 1.0 && lastAsk != bestAsk)
        {
            SingleQuote<Traits> quote{.price = bestAsk, .volume = config->lotSize, .side = 2};

            if (!handler->invoke(tag::Risk::Check, quote))
                return;

            handler->invoke(tag::Stream::SendQuote{}, quote);
            STABLEARB_LOG_INFO(handler, "Quoted ask side with price", bestAsk.template as<double>());
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
            auto price = report.getDecimal<PriceType>("44");
            auto remaining = report.getDecimal<VolumeType>("151");

            orders[orderId] = remaining;
            STABLEARB_LOG_INFO(handler, "New order:", orderId, "with remaining", remaining.str(), '@', price.str());
        }

        // partial/total fill
        if (status == 1 || status == 2)
        {
            auto price = report.getDecimal<PriceType>("44");
            auto remaining = report.getDecimal<VolumeType>("151");
            auto lastRemaining = orders[orderId];
            
            // take-profit order
            auto executed = lastRemaining - remaining; 
            if (executed > 0)
            {
                
            }

            orders[orderId] = remaining;
            STABLEARB_LOG_INFO(handler, "Order filled:", orderId, "with remaining", remaining.str(), '@', price.str());
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

    // <order id, remaining volume>
    boost::unordered_flat_map<std::string, VolumeType> orders;
};

} // namespace stablearb
