#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace usdarb {

template<typename _Router>
struct RouterBase
{
    _Router* router = nullptr;
};

template<typename... Nodes>
struct Router : public Nodes...
{
    using Nodes::Nodes...;

    template<typename... _Nodes>
    Router(_Nodes&&... nodes)
        : Nodes(std::forward<_Nodes&&>(nodes))...
    {
        ((get<_Nodes>().router = this), ...);
    }

    template<typename... Args>
    void dispatch(auto tag, Args&&... args)
    {
        (tryDispatch<Nodes>(tag, std::forward<Args&&>(args)...), ...)
    }

private:
    template<typename Node>
    Node& get()
    {
        static_assert((std::is_same_v<Node, Nodes> || ...));
        return static_cast<Node&>(*this);
    }

    template<typename Node, typename Tag, typename... Args>
    void tryDispatch(Tag tag, Args&&... args)
    {
        if constexpr (requires { std::declval<Node>().handle(tag, std::forward<Args&&>(args)...); })
        {
            get<Node>().handle(tag, std::forward<Args&&>(args)...);
        }
    }
};

} // namespace usdarb
