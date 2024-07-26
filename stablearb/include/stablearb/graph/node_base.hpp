#pragma once

#include "stablearb/data/config.hpp"
#include "stablearb/graph/router_handler.hpp"

namespace stablearb {
// To be optionally inherited by nodes to gain access to configuration
template<typename _Traits, typename Router>
struct NodeBase
{
    using Traits = _Traits;

    NodeBase(Config<Traits> const& config, RouterHandler<Router>& handler)
        : config{&config}
        , handler{&handler}
    {}

    RouterHandler<Router>* getHandler() { return handler; }
    Config<Traits> const* getConfig() { return config; }

    Config<Traits> const* config{nullptr};
    RouterHandler<Router>* handler{nullptr};
};
} // namespace stablearb
