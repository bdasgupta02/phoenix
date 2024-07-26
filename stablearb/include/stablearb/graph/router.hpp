#pragma once

#include "stablearb/data/config.hpp"

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

template<template<typename> class... Nodes>
struct NodeList
{};

template<typename, typename>
struct Router;

template<template<typename> class Node, typename Traits, template<typename> class... Nodes>
Node<Traits>& getNode(Router<Traits, NodeList<Nodes...>>& graph)
{
    static_assert((std::is_same_v<Node<Traits>, Nodes<Traits>> || ...), "Invalid node type");
    return static_cast<Node<Traits>&>(graph);
}

template<typename Node, typename Traits, template<typename> class... Nodes>
Node& getNode(Router<Traits, NodeList<Nodes...>>& graph)
{
    static_assert((std::is_same_v<Node, Nodes<Traits>> || ...), "Invalid node type");
    return static_cast<Node&>(graph);
}

// Static dependency injection wiring router
// Nodes should be descending order in priority
// Injects a traits struct with any relevant types - which blew up complexity a bit woops
template<typename Traits, template<typename> class... Nodes>
struct Router<Traits, NodeList<Nodes...>> : public Nodes<Traits>...
{
    Router(Config<Traits> const& config)
        : Nodes<Traits>(config)...
    {}

    // Use invoke() to dispatch a void call, and retrieve() to dispatch a non-void call
    // The receiver node(s) just need handle() functions to receive the dispatch calls
    // All handle() functions in the receiver nodes should begin with (graph&, tag, ...<args>)

    template<typename Tag, typename... Args>
    void invoke(Tag tag, Args&&... args)
    {
        static_assert(
            (concepts::HasVoidHandler<Nodes<Traits>, Tag, Router, Args...> || ...),
            "At least one node should have this handler");

        (tryInvoke<Nodes>(tag, std::forward<Args&&>(args)...), ...);
    }

    template<typename Tag, typename... Args>
    auto retrieve(Tag tag, Args&&... args)
    {
        static_assert(
            (concepts::HasReturnHandler<Nodes<Traits>, Tag, Router, Args...> ^ ...) == 1,
            "Exactly one node should have this handler");

        return std::forward<decltype(tryRetrieve<Nodes...>(tag, std::forward<Args&&>(args)...))>(
            tryRetrieve<Nodes...>(tag, std::forward<Args&&>(args)...));
    }

private:
    template<template<typename> class Node, typename Tag, typename... Args>
    void tryInvoke(Tag tag, Args&&... args)
    {
        if constexpr (concepts::HasVoidHandler<Node<Traits>, Tag, Router, Args...>)
        {
            auto& node = getNode<Node>(*this);
            node.handle(*this, tag, std::forward<Args&&>(args)...);
        }
    }

    template<template<typename> class FirstNode, template<typename> class... RestNodes, typename Tag, typename... Args>
    auto tryRetrieve(Tag tag, Args&&... args)
    {
        if constexpr (concepts::HasReturnHandler<FirstNode<Traits>, Tag, Router, Args...>)
        {
            auto& node = getNode<FirstNode>(*this);
            return std::forward<decltype(node.handle(*this, tag, std::forward<Args&&>(args)...))>(
                node.handle(*this, tag, std::forward<Args&&>(args)...));
        }
        else
        {
            static_assert(sizeof...(RestNodes) != 0, "Exactly one node should have this handler");
            return std::forward<decltype(tryRetrieve<RestNodes...>(tag, std::forward<Args>(args)...))>(
                tryRetrieve<RestNodes...>(tag, std::forward<Args&&>(args)...));
        }
    }
};

} // namespace stablearb
