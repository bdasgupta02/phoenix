#pragma once

namespace stablearb::tag {

struct Stream
{
    struct Login
    {};

    struct Start
    {};
};

struct Logger
{
    struct Start
    {};

    struct Info
    {};

    struct Warn
    {};

    struct Error
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
    struct MassExit
    {};

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
};

} // namespace stablearb::tag
