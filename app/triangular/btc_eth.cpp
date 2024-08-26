#include "phoenix/common/logger.hpp"
#include "phoenix/common/profiler.hpp"
#include "phoenix/data/decimal.hpp"
#include "phoenix/graph/router.hpp"
#include "phoenix/strategies/triangular/btc_eth/hitter.hpp"
#include "phoenix/strategies/triangular/config.hpp"
#include "phoenix/strategies/triangular/risk.hpp"
#include "phoenix/strategies/triangular/stream.hpp"

using namespace phoenix;
using namespace phoenix::triangular;

struct Traits
{
    using PriceType = Decimal<4u>;
    using VolumeType = Decimal<4u>;
};

// clang-format off
using Graph = Router<
    Config<Traits>,
    Traits,
    NodeList<
        Stream,
        Hitter,
        Risk,
        Logger,
        Profiler
    >
>;
// clang-format on

// Hitter has not been imlpemented yet
// TODO: init this strategy
int main(int argc, char* argv[])
{
    Config<Traits> config;
    if (!config.apply(argc, argv))
        return 1;

    Graph graph{config};
    auto* handler = graph.getHandler();

    handler->invoke(tag::Logger::Start{});
    PHOENIX_LOG_INFO(handler, "Starting BTC/ETH/USDC Triangular Arbitrage System");
    handler->invoke(tag::Stream::Start{});
}
