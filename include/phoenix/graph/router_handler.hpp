#pragma once

#include <memory>

namespace phoenix {

// Maybe a good chance to use C++23's "deducing this"
template<typename Router>
struct RouterHandler
{
    // Use invoke() to dispatch a void call, and retrieve() to dispatch a non-void call
    // The receiver node(s) just need handle() functions to receive the dispatch calls
    // All handle() functions in the receiver nodes should begin with (tag, ...<args>)

    template<typename Tag, typename... Args>
    [[gnu::always_inline, gnu::hot]]
    inline void invoke(Tag tag, Args&&... args)
    {
        static_cast<Router&>(*this).invokeImpl(tag, std::forward<Args>(args)...);
    }

    template<typename Tag, typename... Args>
    [[gnu::always_inline, gnu::hot]]
    inline auto retrieve(Tag tag, Args&&... args)
    {
        return std::forward<decltype(static_cast<Router&>(*this).retrieveImpl(tag, std::forward<Args>(args)...))>(
            static_cast<Router&>(*this).retrieveImpl(tag, std::forward<Args>(args)...));
    }
};

} // namespace phoenix
