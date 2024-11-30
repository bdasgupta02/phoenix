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

struct TCPSocket
{
    struct Connect
    {};

    struct Stop 
    {};

    struct Send
    {};

    struct ForceSend
    {};

    struct Receive
    {};

    struct ForceReceive
    {};

    struct CheckThrottle
    {};

    struct SendUnthrottled
    {};
};

} // namespace phoenix::tag
