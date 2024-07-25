#pragma once

namespace stablearb {

// To be optionally inherited by nodes to gain access to configuration
struct NodeBase
{
    NodeBase(Config const& config)
        : config{&config}
    {}

    Config const* config{nullptr};
};

} // namespace stablearb
