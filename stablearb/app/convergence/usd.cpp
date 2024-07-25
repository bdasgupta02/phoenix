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
    Config config;

    // clang-format off
    Router graph(
        Risk{},
        Stream{
            .config = config
        },
        Logger{},
        Quoter{},
        Profiler{
            .enabled = config.simProfiled
        },
        Sender{
            .config = config
        }
    );
    // clang-format on

    graph.dispatch(tag::Logger::Info{}, "Starting USDC/USDT Convergence Arbitrage System...", true);
    graph.dispatch(tag::Stream::Login{});
    graph.dispatch(tag::Stream::Start{});
}
