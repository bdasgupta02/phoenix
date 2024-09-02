#pragma once

#include <boost/describe/enum_from_string.hpp>
#include <boost/describe/enum_to_string.hpp>
#include <boost/program_options.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace po = boost::program_options;
}

namespace phoenix::sniper {

template<typename Traits>
struct Config
{
    using Volume = Traits::VolumeType;
    using Price = Traits::PriceType;

    bool apply(int argc, char* argv[])
    {
        try
        {
            po::options_description desc("Config");

            double lotsDouble = 1.0;
            double tickSizeDouble = 1.0;

            // clang-format off
            desc.add_options()
                ("help,h", "see all commands")

                // deribit
                ("auth-username", po::value<std::string>(&username)->required(), "Deribit username")
                ("auth-secret", po::value<std::string>(&secret)->required(), "Deribit client secret")
                ("host", po::value<std::string>(&host)->default_value(host), "Deribit host address ([www/test].deribit.com for [prod/test])")
                ("port", po::value<std::string>(&port)->default_value(port), "Deribit port (usually 9881 for TCP)")
                ("client", po::value<std::string>(&client)->default_value(client), "Unique client name")

                // logging
                ("log-level", po::value<LogLevel>(&logLevel)->default_value(logLevel), "Log level [DEBUG, INFO, WARN, ERROR, FATAL]")
                ("log-print", po::value<bool>(&printLogs)->default_value(printLogs), "Print all logs")
                ("log-folder", po::value<std::string>(&logFolder)->required(), "Path to where the log file will be saved")
                ("profiled", po::value<bool>(&profiled)->default_value(profiled), "Profiling mode")

                // settings
                ("instrument", po::value<std::string>(&instrument)->required(), "The traded instrument")
                ("lots", po::value<double>(&lotsDouble)->default_value(lotsDouble), "Lot size to order")
                ("tick-size", po::value<double>(&tickSizeDouble)->default_value(tickSizeDouble), "Min tick size")
                ("contract-size", po::value<double>(&contractSize)->default_value(contractSize), "Contract size for this instrument")
                ("colo", po::value<bool>(&colo)->default_value(colo), "Colo mode")
            ;
            // clang-format on

            po::variables_map vm;
            po::store(po::parse_command_line(argc, argv, desc), vm);

            if (vm.count("help"))
            {
                std::cout << desc << std::endl;
                return false;
            }

            po::notify(vm);

            lots = Volume{lotsDouble};
            tickSize = Price{tickSizeDouble};

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
    std::string secret;
    std::string client;
    std::string host = "www.deribit.com"; // test.deribit.com:9881 for test net
    std::string port = "9881";

    // logging
    std::string logFolder;
    LogLevel logLevel = LogLevel::INFO;
    bool printLogs = false;
    bool profiled = false;

    // settings
    std::string instrument;
    Price tickSize;
    Volume lots;
    double contractSize = 0.0001;
    bool colo = false;
};

} // namespace phoenix::sniper
