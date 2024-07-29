#pragma once

#include "stablearb/data/config.hpp"
#include "stablearb/graph/router_handler.hpp"

namespace stablearb {
// To be optionally inherited by nodes to gain access to configuration
template<typename _Traits, typename _Router>
struct NodeBase
{
    using Traits = _Traits;
    using Router = _Router;

    NodeBase(Config<Traits> const& config, RouterHandler<Router>& handler)
        : config{&config}
        , handler{&handler}
    {}

    [[gnu::always_inline, gnu::hot]]
    inline RouterHandler<Router>* getHandler()
    {
        return handler;
    }

    [[gnu::always_inline, gnu::hot]]
    inline Config<Traits> const* getConfig()
    {
        return config;
    }

    Config<Traits> const* config{nullptr};
    RouterHandler<Router>* handler{nullptr};
};
} // namespace stablearb
