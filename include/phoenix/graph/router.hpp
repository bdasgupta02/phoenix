#pragma once

#include "phoenix/graph/node_base.hpp"
#include "phoenix/graph/router_handler.hpp"
#include "phoenix/strategies/convergence/config.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace phoenix {

namespace concepts {
template<typename T>
concept Void = std::is_void_v<T>;

template<typename T>
concept NonVoid = !Void<T>;

template<typename Node, typename Tag, typename Router, typename... Args>
concept HasVoidHandler = requires(Node node, Tag tag, Router& router, Args&&... args) {
    { node.handle(tag, std::forward<Args&&>(args)...) } -> Void;
};

template<typename Node, typename Tag, typename Router, typename... Args>
concept HasReturnHandler = requires(Node node, Tag tag, Router& router, Args&&... args) {
    { node.handle(tag, std::forward<Args&&>(args)...) } -> NonVoid;
};
} // namespace concepts

template<template<typename> class... Nodes>
struct NodeList
{};

template<typename, typename>
struct Router;

template<template<typename> class Node, typename Traits, template<typename> class... Nodes>
Node<NodeBase<Traits, Router<Traits, NodeList<Nodes...>>>>& getNode(Router<Traits, NodeList<Nodes...>>& graph)
{
    static_assert(
        (std::is_same_v<
             Node<NodeBase<Traits, Router<Traits, NodeList<Nodes...>>>>,
             Nodes<NodeBase<Traits, Router<Traits, NodeList<Nodes...>>>>> ||
         ...),
        "Invalid node type");
    return static_cast<Node<NodeBase<Traits, Router<Traits, NodeList<Nodes...>>>>&>(graph);
}

template<typename Node, typename Traits, template<typename> class... Nodes>
Node& getNode(Router<Traits, NodeList<Nodes...>>& graph)
{
    static_assert(
        (std::is_same_v<Node, Nodes<NodeBase<Traits, Router<Traits, NodeList<Nodes...>>>>> || ...),
        "Invalid node type");
    return static_cast<Node&>(graph);
}

// Static dependency injection wiring router
// Nodes should be descending order in priority
// Statically checks if the called functions are implemented
// Can be simplified a bit more imo
// Note: retrieval functions can return lval or ptr, and are zero-copy
template<typename Traits, template<typename> class... Nodes>
struct Router<Traits, NodeList<Nodes...>>
    : public RouterHandler<Router<Traits, NodeList<Nodes...>>>
    , public Nodes<NodeBase<Traits, Router<Traits, NodeList<Nodes...>>>>...
{
    Router(Config<Traits> const& config)
        : Nodes<NodeBase<Traits, Router>>(config, static_cast<RouterHandler<Router>&>(*this))...
    {}

    RouterHandler<Router>* getHandler() { return static_cast<RouterHandler<Router>*>(this); }

private:
    template<typename Tag, typename... Args>
    [[gnu::always_inline, gnu::hot]]
    inline void invokeImpl(Tag tag, Args&&... args)
    {
        static_assert(
            (concepts::HasVoidHandler<Nodes<NodeBase<Traits, Router>>, Tag, Router, Args...> || ...),
            "At least one node should have this handler");

        (tryInvoke<Nodes>(tag, std::forward<Args&&>(args)...), ...);
    }

    template<typename Tag, typename... Args>
    [[nodiscard, gnu::always_inline, gnu::hot]]
    inline auto retrieveImpl(Tag tag, Args&&... args)
    {
        static_assert(
            (concepts::HasReturnHandler<Nodes<NodeBase<Traits, Router>>, Tag, Router, Args...> ^ ...) == 1,
            "Exactly one node should have this handler");

        return std::forward<decltype(tryRetrieve<Nodes...>(tag, std::forward<Args&&>(args)...))>(
            tryRetrieve<Nodes...>(tag, std::forward<Args&&>(args)...));
    }

    template<template<typename> class Node, typename Tag, typename... Args>
    [[gnu::always_inline, gnu::hot]]
    inline void tryInvoke(Tag tag, Args&&... args)
    {
        if constexpr (concepts::HasVoidHandler<Node<NodeBase<Traits, Router>>, Tag, Router, Args...>)
        {
            auto& node = getNode<Node>(*this);
            node.handle(tag, std::forward<Args&&>(args)...);
        }
    }

    template<template<typename> class FirstNode, template<typename> class... RestNodes, typename Tag, typename... Args>
    [[nodiscard, gnu::always_inline, gnu::hot]]
    inline auto tryRetrieve(Tag tag, Args&&... args)
    {
        if constexpr (concepts::HasReturnHandler<FirstNode<NodeBase<Traits, Router>>, Tag, Router, Args...>)
        {
            auto& node = getNode<FirstNode>(*this);
            return std::forward<decltype(node.handle(tag, std::forward<Args&&>(args)...))>(
                node.handle(tag, std::forward<Args&&>(args)...));
        }
        else
        {
            static_assert(sizeof...(RestNodes) != 0, "No nodes have this handler");
            return std::forward<decltype(tryRetrieve<RestNodes...>(tag, std::forward<Args&&>(args)...))>(
                tryRetrieve<RestNodes...>(tag, std::forward<Args&&>(args)...));
        }
    }

    template<typename Tag, typename... Args>
    friend void RouterHandler<Router>::invoke(Tag tag, Args&&... args);

    template<typename Tag, typename... Args>
    friend auto RouterHandler<Router>::retrieve(Tag tag, Args&&... args);
};

} // namespace phoenix
