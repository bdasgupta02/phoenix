#pragma once

namespace stablearb {

struct Profiler
{
    template<typename Router>
    struct Timer
    {
        Timer(Router& router, bool enabled)
            : enabled(enabled)
            , router(router)
        {}

        ~Timer() {}

        bool enabled = false;
        Router& router;
    };

    auto handle(auto& router, tag::Profiler::Guard) { return Timer(router, enabled); }

    bool enabled = false;
};

} // namespace stablearb
