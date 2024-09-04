#pragma once

namespace phoenix::tag {

struct Stream
{
    struct Start
    {};

    struct Stop
    {};

    struct SendQuotes
    {};

    struct TakeMarketOrders
    {};

    struct CancelQuote
    {};

    struct GetBalance
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

    struct CSV
    {};
};

struct Profiler
{
    struct Guard
    {};
};

struct Quoter
{
    struct MDUpdate
    {};

    struct ExecutionReport
    {};
};

struct Hitter
{
    struct MDUpdate
    {};

    struct ExecutionReport
    {};

    struct InitBalances
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

} // namespace phoenix::tag
