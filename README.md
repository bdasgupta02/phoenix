# Phoenix
Arbitrage strategies and experiments for cryptocurrency. Mostly meant for fun.

## Strategies
### Equivalent coin pair convergence arbitrage
- Arbitrage based market-making with a bias towards a price of 1 - without any latency requirement due to illiquid markets
- Low-risk and deterministic, but very low upside
- e.g. USDC/USDT, STETH/ETH

### Triangular arbitrage across multiple books for equivalent coins
- Instantly capturing edge between 3 books
- e.g. BTC/USDC vs BTC/USDT vs. USDC/USDT
- Very latency sensitive, and slippage can be an issue without colocation
- Uses aggressor limit orders, as market orders have an extreme and unreasonable degree of slippage (I suspect Deribit's matching engine has a separate queue for market orders)

## Exchange
Deribit with TCP and FIX protocol. The exchange has a weird quirk where it reflects the extreme volatility of these overarching crypto spot markets, but is illiquid with very wide spreads and low daily volumes (BTC/USDC is the highest volume spot pair for instance, with just 6 mil USD a day). Definitely provides a nice unique challenge to have fun with. There's random spikes in volume for spots however, for instance USDC/USDT gets tons of activity in volatile BTC markets when big players try to offload their position or hedge derivatives. There's tons of room for experimentation here.

## Colocation 
This project supports colocation with Deribit's matching engine in LD4, especially necessary for the triangular arbitrage strategy

## Tech stack
- Arch Linux
- x86_64
- GCC 14.1.1
- C++ 20

## Static dependency injection
This project also includes a very overkill but small implementation for an automatic wiring system for static dependency injection. My design tries to simplify the end-user interface at the expense of some compile time. Just create a graph like below, and construct/run it like magic:

### Creating nodes
```cpp
// NodeBase is a lightweight struct to expose:
// 1. Pointer to the app config struct 
// 2. Handler to invoke functions of other nodes *magically*
template<typename NodeBase>
struct Stream : NodeBase 
{
    // The router calls the base constructor
    using NodeBase::NodeBase;

    // To receive function calls, just use handle(tag) functions 
    // These handlers support any number of universal ref arguments
    // Note: if multiple nodes have the same handler function signatures, 
    // they both will receive the function call only for void handlers
    void handle(tag::Stream::Start, &&...)
    {
        ...

        // To call other nodes:
        // 1. and retrieve() for functions that return something
        // 2. use invoke() for void handlers 

        // calls quoter's "Position" handle() function to get some value
        auto position = this->getHandler()->retrieve(tag::Quoter::Position{}); 

        // call's risk's "Evaluate" handle() function
        this->getHandler()->invoke(tag::Risk::Evaluate{}, position);
    }

    // Handlers can also return anything if needed
    Status handle(tag::Stream::Status, &&...) 
    {
        // Access the app config in any node easily
        auto* config = this->getConfig();

        ...

        return status;
    }

    ...
};

```

### Construction of the graph
```cpp
// Finally, create a graph with a config, type traits and a node list
using Graph = Router<
    Config,
    Traits,
    NodeList<
        Risk,
        Stream,
        Quoter,
        Logger,
        ...
    >
>;

// Construct it as follows with a config
Graph graph{config};

// Call any node handler functions from the outside to start the app,
// just like you would from within any node
auto* handler = graph.getHandler();
handler->invoke(tag::Stream::Start{});
```
