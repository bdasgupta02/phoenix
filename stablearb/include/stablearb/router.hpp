#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace stablearb {

template<typename... Nodes>
struct Router;

template<typename Node, typename... Nodes>
Node& getNode(Router<Nodes...>& graph)
{
    static_assert((std::is_same_v<Node, Nodes> || ...));
    return static_cast<Node&>(graph);
}

// Static dependency injection wiring router
// Nodes should be descending order in priority
template<typename... Nodes>
struct Router : public Nodes...
{
    using Nodes::Nodes...;

    Router(Nodes&&... nodes)
        : Nodes(std::forward<Nodes&&>(nodes))...
    {}

    template<typename... Args>
    void dispatch(auto tag, Args&&... args)
    {
        (tryDispatch<Nodes>(tag, std::forward<Args&&>(args)...), ...);
    }

private:
    // Handlers should start with (graph&, tag, ...)
    template<typename Node, typename... Args>
    void tryDispatch(auto tag, Args&&... args)
    {
        if constexpr (requires { std::declval<Node>().handle(this, tag, std::forward<Args&&>(args)...); })
        {
            auto& node = getNode<Node>(*this);
            node.handle(*this, tag, std::forward<Args&&>(args)...);
        }
    }
};

} // namespace stablearb
