#pragma once

#include "stablearb/data/config.hpp"
#include "stablearb/graph/router_handler.hpp"

namespace stablearb {

// To be optionally inherited by nodes to gain access to configuration
template<typename Traits, typename Router>
struct NodeBase
{
    NodeBase(Config<Traits> const& config, RouterHandler<Router>& handler)
        : config{&config}
        , handler{&handler}
    {}

    RouterHandler<Router>* getHandler() { return handler; }

    Config<Traits> const* config{nullptr};
    RouterHandler<Router>* handler{nullptr};
};

} // namespace stablearb
