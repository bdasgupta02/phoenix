#pragma once

namespace stablearb::tag {

struct Stream
{
    struct Start
    {};

    struct Stop
    {};

    struct SendQuote
    {};
};

struct Logger
{
    struct Start
    {};

    struct Stop
    {};

    struct Log
    {};

    struct Verify
    {};
};

struct Profiler
{
    struct Guard
    {};
};

struct Quoter
{
    struct Quote
    {};

    struct ExecutionReport
    {};
};

struct Risk
{
    struct Abort
    {};
    
    struct Check
    {};
    
    struct UpdatePosition
    {};
};

} // namespace stablearb::tag
