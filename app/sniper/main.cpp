#include "phoenix/common/logger.hpp"
#include "phoenix/common/profiler.hpp"
#include "phoenix/data/decimal.hpp"
#include "phoenix/graph/router.hpp"
#include "phoenix/strategies/sniper/config.hpp"
#include "phoenix/strategies/sniper/hitter.hpp"
#include "phoenix/strategies/sniper/risk.hpp"
#include "phoenix/strategies/sniper/stream.hpp"
#include "phoenix/utils.hpp"

using namespace phoenix;
using namespace phoenix::sniper;

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
        Stream,
        Hitter,
        Risk,
        Logger,
        Profiler
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

    setMaxThreadPriority();

    handler->invoke(tag::Logger::Start{});
    PHOENIX_LOG_INFO(handler, "Starting ETH/STETH/USDC Triangular Arbitrage System");
    handler->invoke(tag::Stream::Start{});
}
