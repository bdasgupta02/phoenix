#pragma once

#include "phoenix/graph/router_handler.hpp"

namespace phoenix {
template<typename _Traits, typename _Router, typename _Config>
struct NodeBase
{
    using Traits = _Traits;
    using Router = _Router;
    using Config = _Config;

    NodeBase(Config const& config, RouterHandler<Router>& handler)
        : config{&config}
        , handler{&handler}
    {}

    [[gnu::always_inline, gnu::hot]]
    inline RouterHandler<Router>* getHandler()
    {
        return handler;
    }

    [[gnu::always_inline, gnu::hot]]
    inline Config const* getConfig()
    {
        return config;
    }

    Config const* config{nullptr};
    RouterHandler<Router>* handler{nullptr};
};
} // namespace phoenix
