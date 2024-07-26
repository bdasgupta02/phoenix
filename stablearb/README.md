# Stable Arb
- Arbitrage over stable pairs (stablecoins or equivalent crypto coins)
- Made to be flexible for arbitrage on any delta-1 product
- These strategies mostly don't rely on quickly fleeting opportunities
- Meant as a fun experiment

## Strategies
### Equivalent coin pair convergence arbitrage
- High-frequency arbitrage based market-making with a bias towards a price of 1 - but most advantage is lost by running over the internet.
- e.g. USDC/USDT, STETH/ETH

## Static dependency injection
This project also includes a very overkill but small implementation for an automatic wiring system for static dependency injection. My design tries to simplify the end-user interface at the expense of some compile time. Just Make a graph like below, and construct/run it like magic:

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
    void handle(tag::Stream::Start)
    {
        ...

        // To call other nodes:
        // 1. and retrieve() for functions that return something
        // 2. use invoke() for void handlers 
        auto& position = this->getHandler()->retrieve(tag::Quoter::Position{});
        this->getHandler()->invoke(tag::Risk::Evaluate{}, position);
    }

    // Handlers can also return anything if needed 
    Status& handle(tag::Stream::Status, ...) 
    {
        return status;
    }

    ...
};

```

### Construction of the graph
```cpp
// Finally, create a graph with type traits and a node list
using Graph = Router<
    Traits,
    NodeList<
        Risk,
        Stream,
        Quoter,
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

## Exchange
Deribit with TCP and FIX protocol

## Tech stack
- Arch Linux x86_64
- GCC 14.1.1
- C++ 23
