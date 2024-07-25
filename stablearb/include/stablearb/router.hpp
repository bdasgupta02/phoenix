#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace stablearb {

template<typename... Nodes>
struct Router;

template<typename Node, typename Tag, typename _Router, typename... Args>
concept HasHandler = requires(Node node, Tag tag, _Router& router, Args&&... args) {
    node.handle(router, tag, std::forward<Args&&>(args)...);
};

template<typename Node, typename... Nodes>
Node& getNode(Router<Nodes...>& graph)
{
    static_assert((std::is_same_v<Node, Nodes> || ...), "Invalid node type");
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

    template<typename Tag, typename... Args>
    void dispatch(Tag tag, Args&&... args)
    {
        static_assert((HasHandler<Nodes, Tag, Router&, Args...> || ...), "At least one node should have this handler");
        (tryDispatch<Nodes>(tag, std::forward<Args&&>(args)...), ...);
    }

private:
    // Handlers should start with (graph&, tag, ...)
    template<typename Node, typename Tag, typename... Args>
    void tryDispatch(Tag tag, Args&&... args)
    {
        if constexpr (HasHandler<Node, Tag, Router&, Args...>)
        {
            auto& node = getNode<Node>(*this);
            node.handle(*this, tag, std::forward<Args&&>(args)...);
        }
    }
};

} // namespace stablearb
