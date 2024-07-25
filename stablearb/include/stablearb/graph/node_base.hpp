#pragma once

namespace stablearb {

struct NodeBase
{
    NodeBase(Config const& config)
        : config{&config}
    {}

    Config const* config{nullptr};
};

} // namespace stablearb
