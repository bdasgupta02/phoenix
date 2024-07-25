#include "stablearb/common/logger.hpp"
#include "stablearb/common/profiler.hpp"
#include "stablearb/common/sender.hpp"
#include "stablearb/common/stream.hpp"
#include "stablearb/convergence/quoter.hpp"
#include "stablearb/convergence/risk.hpp"
#include "stablearb/data/config.hpp"
#include "stablearb/data/price.hpp"
#include "stablearb/graph/router.hpp"
#include "stablearb/tags.hpp"

using namespace stablearb;
namespace po = boost::program_options;

struct Traits
{
    using PriceType = Price<4u>;
};

// clang-format off
using Graph = Router<
    Risk<Traits>,
    Stream<Traits>,
    Quoter<Traits>,
    Sender,
    Profiler,
    Logger
>;
// clang-format on

int main(int argc, char* argv[])
{
    Config config;
    if (!config.apply(argc, argv))
        return 1;

    Graph graph{config};

    graph.invoke(tag::Logger::Start{});
    STABLEARB_LOG_INFO_PRINT(graph, "Starting USDC/USDT Convergence Arbitrage System");

    graph.invoke(tag::Stream::Login{});
    graph.invoke(tag::Stream::Start{});
}
