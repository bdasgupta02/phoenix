#include "stablearb/common/logger.hpp"
#include "stablearb/common/profiler.hpp"
#include "stablearb/common/sender.hpp"
#include "stablearb/common/stream.hpp"
#include "stablearb/convergence/quoter.hpp"
#include "stablearb/convergence/risk.hpp"
#include "stablearb/data/config.hpp"
#include "stablearb/data/decimal.hpp"
#include "stablearb/graph/router.hpp"
#include "stablearb/tags.hpp"

using namespace stablearb;

struct Traits
{
    using PriceType = Decimal<4u>;
};

// clang-format off
using Graph = Router<
    Traits,
    NodeList<
        Risk,
        Stream,
        Quoter,
        Sender,
        Profiler,
        Logger
    >
>;
// clang-format on

int main(int argc, char* argv[])
{
    Config<Traits> config;
    if (!config.apply(argc, argv))
        return 1;

    Graph graph{config};
    auto* handler = graph.getHandler();

    handler->invoke(tag::Logger::Start{});
    STABLEARB_LOG_INFO_PRINT(handler, "Starting Convergence Arbitrage System");
    handler->invoke(tag::Stream::Start{});
}
