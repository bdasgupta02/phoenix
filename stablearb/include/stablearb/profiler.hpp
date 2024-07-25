#pragma once

#include "stablearb/tags.hpp"

#include <chrono>

namespace stablearb {

struct Profiler
{
    template<typename Router>
    struct Timer
    {
        Timer(Router& router, bool enabled, std::string name)
            : enabled(enabled)
            , router(router)
            , name(name)
            , start(std::chrono::high_resolution_clock::now())
        {}

        ~Timer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            router.invoke(tag::Logger::Info{}, name + " took " + std::string{duration.count()} + "µs");
        }

        bool enabled = false;
        Router& router;
        std::string name;
        std::chrono::high_resolution_clock::time_point start;
    };

    auto handle(auto& router, tag::Profiler::Guard, std::string name) { return Timer(router, enabled, name); }

    bool enabled = false;
};

} // namespace stablearb
