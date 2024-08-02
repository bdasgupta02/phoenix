#include "phoenix/common/logger.hpp"
#include "phoenix/common/profiler.hpp"
#include "phoenix/data/decimal.hpp"
#include "phoenix/graph/router.hpp"
#include "phoenix/strategies/convergence/config.hpp"
#include "phoenix/strategies/convergence/quoter.hpp"
#include "phoenix/strategies/convergence/risk.hpp"
#include "phoenix/strategies/convergence/stream.hpp"
#include "phoenix/tags.hpp"

using namespace phoenix;
using namespace phoenix::convergence;

struct Traits
{
    using PriceType = Decimal<4u>;
    using VolumeType = Decimal<0u>;
};

// clang-format off
using Graph = Router<
    Config<Traits>,
    Traits,
    NodeList<
        Risk,
        Stream,
        Quoter,
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
    PHOENIX_LOG_INFO(handler, "Starting ETH Convergence Arbitrage System");
    handler->invoke(tag::Stream::Start{});
}
