#include "stablearb/logger.hpp"
#include "stablearb/profiler.hpp"
#include "stablearb/router.hpp"
#include "stablearb/sender.hpp"
#include "stablearb/sim.hpp"
#include "stablearb/stream.hpp"
#include "stablearb/price.hpp"
#include "stablearb/convergence/quoter.hpp"
#include "stablearb/convergence/risk.hpp"

#include <boost/program_options/options_description.hpp>

using namespace stablearb;
using namespace stablearb::convergence;
namespace po = boost::program_options;

using PriceType = Price<4u>; 

struct ProgramOptions
{
    ProgramOptions() {}

    bool sim = false;
    bool simProfiled = false;
    double txFee = 0.00;
    int aggressiveTicks = 1; // negative for non-aggressive
};

template<typename... Args>
void start(Args&&... args)
{
    Router graph(std::forward<Args&&>(args)...);

    auto& stream = getNode<Stream>(graph);
    auto& logging = getNode<Logger>(graph);

    logging.info("Starting USDC/USDT Convergence Arbitrage System...", true);
    stream.login();
    stream.start(graph);
}

int main()
{
    ProgramOptions options;

    Risk risk{};
    Stream stream{};
    Logger logger{};
    Quoter quoter{};

    if (options.simProfiled)
    {
        Sim sim{};
        Profiler profiler{};
        start(
            std::move(risk),
            std::move(stream),
            std::move(logger),
            std::move(quoter),
            std::move(sim),
            std::move(profiler));

        return 0;
    }

    if (options.sim)
    {
        Sim sim{};
        start(std::move(risk), std::move(stream), std::move(logger), std::move(quoter), std::move(sim));
        return 0;
    }

    Sender sender{};
    start(std::move(risk), std::move(stream), std::move(logger), std::move(quoter), std::move(sender));
}
