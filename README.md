# Phoenix
Arbitrage strategies and experiments for cryptocurrency. Mostly meant for fun.

## Strategies
### Equivalent coin pair convergence arbitrage
- Arbitrage based market-making with a bias towards a price of 1 - without much latency requirement due to illiquid markets.
- e.g. USDC/USDT, STETH/ETH

### [In-Progress] Triangular arbitrage across multiple books for equivalent coins
- Fast market orders to instantly capture opportunities across 3 books.
- e.g. BTC/USDC vs BTC/USDT vs. USDC/USDT
- More latency sensitive than the first one

## Exchange
Deribit with TCP and FIX protocol

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
