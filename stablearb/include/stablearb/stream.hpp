#pragma once

namespace stablearb {

struct Stream
{
    ~Stream()
    {
        // mass exit, mass cancel, logout, disconnect on dtor here
    }

    void login()
    {
        // wrap in try catch and log
    }

    void start(auto& graph)
    {
        // wrap in try catch and log
    }
};

} // namespace stablearb
