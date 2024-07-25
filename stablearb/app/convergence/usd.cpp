#include "stablearb/config.hpp"
#include "stablearb/convergence/quoter.hpp"
#include "stablearb/convergence/risk.hpp"
#include "stablearb/logger.hpp"
#include "stablearb/price.hpp"
#include "stablearb/profiler.hpp"
#include "stablearb/router.hpp"
#include "stablearb/sender.hpp"
#include "stablearb/stream.hpp"
#include "stablearb/tags.hpp"

using namespace stablearb;
using namespace stablearb::convergence;
namespace po = boost::program_options;

struct Traits
{
    using PriceType = Price<4u>;
};

int main()
{
    Config config{.appName = "USDC-USDT-Arb"};

    // clang-format off
    Router graph(
        Risk{},
        Stream{
            .config = &config
        },
        Quoter{
            .config = &config
        },
        Sender{
            .config = &config
        },
        Profiler{
            .enabled = config.simProfiled
        },
        Logger{}
    );
    // clang-format on

    graph.invoke(tag::Logger::Start{}, config.appName);
    STABLEARB_LOG_INFO_PRINT(graph, "Starting USDC/USDT Convergence Arbitrage System");

    graph.invoke(tag::Stream::Login{});
    graph.invoke(tag::Stream::Start{});
}
