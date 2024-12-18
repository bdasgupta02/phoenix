#include "phoenix/common/logger.hpp"
#include "phoenix/graph/router.hpp"
#include "phoenix/strategies/data/config.hpp"
#include "phoenix/strategies/data/risk.hpp"
#include "phoenix/strategies/data/stream.hpp"
#include "phoenix/utils.hpp"

#include <iostream>

// Dataset generator from MD for analysis or modelling

using namespace phoenix;
using namespace phoenix::data;

struct DummyTraits
{};

// clang-format off
using Graph = Router<
    Config<DummyTraits>,
    DummyTraits,
    NodeList<
        Stream,
        Logger,
        Risk
    >
>;
// clang-format on

int main(int argc, char* argv[])
{
    Config<DummyTraits> config;
    if (!config.apply(argc, argv))
        return 1;

    Graph graph{config};
    auto* handler = graph.getHandler();

    setMaxThreadPriority();

    handler->invoke(tag::Logger::Start{}, true);
    std::cout << "Starting data reader" << std::endl;
    handler->invoke(tag::Stream::Start{});
}
