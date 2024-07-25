#pragma once

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace stablearb {

namespace concepts {
template<typename T>
concept Void = std::is_void_v<T>;

template<typename T>
concept NonVoid = !Void<T>;

template<typename Node, typename Tag, typename Router, typename... Args>
concept HasVoidHandler = requires(Node node, Tag tag, Router& router, Args&&... args) {
    { node.handle(router, tag, std::forward<Args&&>(args)...) } -> Void;
};

template<typename Node, typename Tag, typename Router, typename... Args>
concept HasReturnHandler = requires(Node node, Tag tag, Router& router, Args&&... args) {
    { node.handle(router, tag, std::forward<Args&&>(args)...) } -> NonVoid;
};
} // namespace concepts

template<typename... Nodes>
struct Router;

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
        : Nodes(std::forward<Nodes>(nodes))...
    {}

    // Use invoke() to dispatch a void call, and retrieve() to dispatch a non-void call

    template<typename Tag, typename... Args>
    void invoke(Tag tag, Args&&... args)
    {
        static_assert(
            (concepts::HasVoidHandler<Nodes, Tag, Router&, Args...> || ...),
            "At least one node should have this handler");

        (tryInvoke<Nodes>(tag, std::forward<Args>(args)...), ...);
    }

    template<typename Tag, typename... Args>
    auto retrieve(Tag tag, Args&&... args)
    {
        static_assert(
            (concepts::HasReturnHandler<Nodes, Tag, Router&, Args...> ^ ...) == 1,
            "Exactly one node should have this handler");

        return tryRetrieve<Nodes...>(tag, std::forward<Args>(args)...);
    }

private:
    // Handlers should start with (graph&, tag, ...)
    template<typename Node, typename Tag, typename... Args>
    void tryInvoke(Tag tag, Args&&... args)
    {
        if constexpr (concepts::HasVoidHandler<Node, Tag, Router&, Args...>)
        {
            auto& node = getNode<Node>(*this);
            node.handle(*this, tag, std::forward<Args>(args)...);
        }
    }

    template<typename FirstNode, typename... RestNodes, typename Tag, typename... Args>
    auto tryRetrieve(Tag tag, Args&&... args)
    {
        static_assert(sizeof...(RestNodes) != 0, "Exactly one node should have this handler");
        if constexpr (concepts::HasReturnHandler<FirstNode, Tag, Router&, Args...>)
        {
            auto& node = getNode<FirstNode>(*this);
            return node.handle(*this, tag, std::forward<Args>(args)...);
        }
        else
            return tryRetrieve<RestNodes...>(tag, std::forward<Args>(args)...);
    }
};

} // namespace stablearb
