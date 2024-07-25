#pragma once

#include "stablearb/config.hpp"
#include "stablearb/tags.hpp"

namespace stablearb {

struct Stream
{
    ~Stream()
    {
        // mass exit, mass cancel, logout, disconnect on dtor here
    }

    void handle(auto& graph, tag::Stream::Login)
    {
        // wrap in try catch and log
    }

    void handle(auto& graph, tag::Stream::Start)
    {
        // wrap in try catch and log
    }

    Config* config;
};

} // namespace stablearb
