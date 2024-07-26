#pragma once

namespace stablearb::tag {

struct Stream
{
    struct Login
    {};

    struct Start
    {};

    struct Stop
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
    struct Disable
    {};

    struct BookUpdate
    {};
};

struct Sender
{
    struct MassCancel
    {};

    struct MassQuote
    {};

    struct Quote
    {};

    struct TargetReset
    {};
};

struct Risk
{
    struct Fill
    {};

    struct BookUpdate
    {};

    struct Error
    {};

    struct Abort
    {};
};

} // namespace stablearb::tag
