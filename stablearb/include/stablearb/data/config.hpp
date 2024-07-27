#pragma once

#include "stablearb/enums/log_level.hpp"

#include <boost/describe/enum_from_string.hpp>
#include <boost/describe/enum_to_string.hpp>
#include <boost/program_options.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace po = boost::program_options;
}

namespace stablearb {

template<typename Traits>
struct Config
{
    using PriceType = Traits::PriceType;

    bool apply(int argc, char* argv[])
    {
        try
        {
            po::options_description desc(appName + " Arbitrage System Config");

            double lotSizeDouble{0.00};

            // clang-format off
            desc.add_options()
                ("help,h", "see all commands")
                ("username", po::value<std::string>(&username)->required(), "Deribit username")
                ("password", po::value<std::string>(&password)->required(), "Deribit password")
                ("auth-nonce", po::value<std::string>(&nonce)->required(), "Deribit Base64 raw nonce data")
                ("host", po::value<std::string>(&host)->default_value(host), "Deribit host address ([www/test].deribit.com for [prod/test])")
                ("port", po::value<std::string>(&port)->default_value(port), "Deribit port (usually 9881 for TCP)")
                ("appName", po::value<std::string>(&appName)->required(), "Application name")
                ("instrument", po::value<std::string>(&instrument)->required(), "Instrument name")
                ("lotSize", po::value<double>(&lotSizeDouble), "Quote lot size mininimum increment")
                ("kind", po::value<std::string>(&kind)->required(), "Instrument kind")
                ("profiled", po::value<bool>(&profiled)->default_value(profiled), "Profiling mode")
                ("fee", po::value<double>(&fee)->default_value(fee), "Fee percentage")
                ("exit-ticks", po::value<std::uint32_t>(&exitTicks)->default_value(exitTicks), "Aggressive ticks to take profit and exit position")
                ("inventory-limit", po::value<std::uint32_t>(&inventoryLimit)->default_value(inventoryLimit), "Total inventory limit")
                ("log-level", po::value<LogLevel>(&logLevel)->default_value(logLevel), "Log level [DEBUG, INFO, WARN, ERROR, FATAL]")
            ;
            // clang-format on

            po::variables_map vm;
            po::store(po::parse_command_line(argc, argv, desc), vm);

            if (vm.count("help"))
            {
                std::cout << desc << std::endl;
                return false;
            }

            lotSize = PriceType{lotSizeDouble};

            po::notify(vm);
            return true;
        }
        catch (po::error const& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            return false;
        }
        catch (std::exception const& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            return false;
        }
    }

    // deribit connectivity
    std::string username;
    std::string password;
    std::string nonce;
    std::string host = "www.deribit.com"; // test.deribit.com:9881 for test net
    std::string port = "9881";

    // app
    std::string appName;
    std::string instrument;
    std::string kind;
    PriceType lotSize;
    bool profiled = false;
    double fee = 0.00;
    std::uint32_t exitTicks = 1;
    std::uint32_t inventoryLimit = 50;
    LogLevel logLevel = LogLevel::INFO;
};

} // namespace stablearb
