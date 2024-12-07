#pragma once

#include "phoenix/data/fix.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace phoenix {

namespace detail {
template<typename T>
struct NodePool
{
    using value_type = T;
    using size_type = std::size_t;

    static NodePool& getInstance()
    {
        static NodePool instance{true};
        return instance;
    }

    NodePool() = default;

    value_type* allocate(std::size_t n)
    {
        assert(n == 1u);
        auto& self = getInstance();

        auto curr = self.index;
        while (!self.pool[curr].isFree)
            curr = (curr + 1u) & PREALLOCATOR_MASK;

        self.index = (curr + 1u) & PREALLOCATOR_MASK;
        auto* freeNode = &self.pool[curr];
        freeNode->isFree = false;
        return &freeNode->value;
    }

    void deallocate(value_type* node, std::size_t)
    {
        auto& self = getInstance();
        auto it = self.addresses.find(node);
        assert(it != self.addresses.end());
        self.pool[it->second].isFree = true;
    }

private:
    NodePool(bool)
    {
        pool.resize(PREALLOCATOR_SIZE);
        for (std::size_t i = 0u; i < PREALLOCATOR_SIZE; ++i)
            addresses[&(pool[i].value)] = i;
        index = 0u;
    }

    // assuming std::map doesn't do pointer arithmetic
    struct Node 
    {
        value_type value; 
        bool isFree = true;
    };

    static constexpr std::size_t PREALLOCATOR_SIZE = 65536u;
    static constexpr std::size_t PREALLOCATOR_MASK = PREALLOCATOR_SIZE - 1u;
    std::vector<Node> pool;
    boost::unordered_flat_map<value_type const*, std::size_t> addresses;
    std::size_t index = 0u;
};
}

template<typename Price, typename Volume>
struct OrderBook
{
    using Allocator = detail::NodePool<std::pair<Price const, Volume>>;
    using BidMap = std::map<Price, Volume, std::greater<>, Allocator>;
    using AskMap = std::map<Price, Volume, std::less<>, Allocator>;

    void pushBid(Price price, Volume volume) { bids[price] = volume; }
    void pushAsk(Price price, Volume volume) { asks[price] = volume; }

    void popBid(Price price) { bids.erase(price); }
    void popAsk(Price price) { asks.erase(price); }

    auto getBestBid() const
    {
        /*assert(!bids.empty());*/
        auto it = bids.begin();
        return *it;
    }

    auto getBestAsk() const
    {
        /*assert(!asks.empty());*/
        auto it = asks.begin();
        return *it;
    }

    auto getNthBestBid(std::size_t n) const
    {
        /*assert(n < bids.size());*/
        auto it = bids.begin();
        std::advance(it, n);
        return *it;
    }

    auto getNthBestAsk(std::size_t n) const
    {
        /*assert(n < asks.size());*/
        auto it = asks.begin();
        std::advance(it, n);
        return *it;
    }

    void fromSnapshot(FIXReaderFast& reader)
    {
        std::size_t const numUpdates = reader.getNumber<std::size_t>(268);
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = reader.getNumber<unsigned>(269, i);

            auto const price = reader.getDecimal<Price>(270, i);
            auto const volume = reader.getDecimal<Volume>(271, i);

            if (typeField == 0u)
                pushBid(price, volume);
            else if (typeField == 1u)
                pushAsk(price, volume);
        }
    }

    void fromUpdate(FIXReaderFast& reader)
    {
        std::size_t const numUpdates = reader.getNumber<std::size_t>(268);
        for (std::size_t i = 0u; i < numUpdates; ++i)
        {
            unsigned const typeField = reader.getNumber<unsigned>(269, i);
            unsigned const actionField = reader.getNumber<unsigned>(279, i);

            auto const price = reader.getDecimal<Price>(270, i);
            auto const volume = reader.getDecimal<Volume>(271, i);

            if (typeField == 0u)
            {
                if (actionField == 2u)
                    popBid(price);
                else 
                    pushBid(price, volume);
            }
            else if (typeField == 1u)
            {
                if (actionField == 2u)
                    popAsk(price);
                else 
                    pushAsk(price, volume);
            }
        }
    }

private:
    BidMap bids;
    AskMap asks;
};

} // namespace phoenix
