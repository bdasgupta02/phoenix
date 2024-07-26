#pragma once

namespace stablearb {

// To be optionally inherited by nodes to gain access to configuration
template<typename Traits>
struct NodeBase
{
    NodeBase(Config<Traits> const& config)
        : config{&config}
    {}

    Config<Traits> const* config{nullptr};
};

} // namespace stablearb
