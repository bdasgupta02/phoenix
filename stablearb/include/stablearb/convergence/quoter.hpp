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

    // TODO: handle case where only one side is available - now it leads to fatal
    inline void handle(tag::Quoter::Quote, FIXReader&& topLevel, std::size_t seqNum)
    {
        auto* handler = this->getHandler();
        auto* config = this->getConfig();

        PriceType lastBid = bestBid;
        PriceType lastAsk = bestAsk;

        if (topLevel.getNumber<unsigned int>("269", 0) == 0)
        {
            bestBid = topLevel.getPrice<PriceType>("270", 0);
            bestAsk = topLevel.getPrice<PriceType>("270", 1);
        }
        else if (topLevel.getNumber<unsigned int>("269", 1) == 0)
        {
            bestBid = topLevel.getPrice<PriceType>("270", 1);
            bestAsk = topLevel.getPrice<PriceType>("269", 0);
        }
        else
        {
            STABLEARB_LOG_ERROR(handler, "Invalid top level market data: cannot find bid");
            return;
        }

        if (bestBid < 1.0 && lastBid != bestBid)
        {
            STABLEARB_LOG_INFO(handler, "Quoting bid side with price", bestBid.template as<double>());
            SingleQuote<Traits> quote{.price = bestBid, .volume = config->lotSize, .side = 0};
            handler->invoke(tag::Stream::SendQuote{}, quote);
        }

        if (bestAsk > 1.0 && lastAsk != bestAsk)
        {
            STABLEARB_LOG_INFO(handler, "Quoting ask side with price", bestAsk.template as<double>());
            SingleQuote<Traits> quote{.price = bestAsk, .volume = config->lotSize, .side = 2};
            handler->invoke(tag::Stream::SendQuote{}, quote);
        }
    }

    inline void handle(tag::Quoter::ExecutionReport, FIXReader&& report) {}

    PriceType lastQuote;
    PriceType bestBid;
    PriceType bestAsk;

    // <order id, quote state>
    boost::unordered_flat_map<std::string, QuoteState> quotes;
};

} // namespace stablearb
