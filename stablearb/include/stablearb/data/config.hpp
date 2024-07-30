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
    using VolumeType = Traits::VolumeType;

    bool apply(int argc, char* argv[])
    {
        try
        {
            po::options_description desc("Config");

            double lotSizeDouble;
            double tickSizeDouble;

            // clang-format off
            desc.add_options()
                ("help,h", "see all commands")
                ("auth-username", po::value<std::string>(&username)->required(), "Deribit username")
                ("auth-secret", po::value<std::string>(&secret)->required(), "Deribit client secret")
                ("host", po::value<std::string>(&host)->default_value(host), "Deribit host address ([www/test].deribit.com for [prod/test])")
                ("port", po::value<std::string>(&port)->default_value(port), "Deribit port (usually 9881 for TCP)")
                ("instrument", po::value<std::string>(&instrument)->required(), "Instrument name")
                ("lot-size", po::value<double>(&lotSizeDouble)->required(), "Quote lot size")
                ("tick-size", po::value<double>(&tickSizeDouble)->required(), "Minimum tick size")
                ("kind", po::value<std::string>(&kind)->required(), "Instrument kind")
                ("aggressive", po::value<bool>(&aggressive)->default_value(aggressive), "Aggressive mode")
                ("profiled", po::value<bool>(&profiled)->default_value(profiled), "Profiling mode")
                ("position-limit", po::value<double>(&positionBoundary)->default_value(positionBoundary), "One sided quote position limit")
                ("log-level", po::value<LogLevel>(&logLevel)->default_value(logLevel), "Log level [DEBUG, INFO, WARN, ERROR, FATAL]")
                ("log-print", po::value<bool>(&printLogs)->default_value(printLogs), "Print all logs")
                ("log-folder", po::value<std::string>(&logFolder)->required(), "Path to where the log file will be saved")
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
            lotSize = VolumeType{lotSizeDouble};
            tickSize = PriceType{tickSizeDouble};

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
    std::string host = "www.deribit.com"; // test.deribit.com:9881 for test net
    std::string port = "9881";

    // app
    std::string instrument;
    std::string kind;
    std::string logFolder;

    VolumeType lotSize;
    PriceType tickSize;

    bool aggressive = true;
    double positionBoundary = 20.0;
    bool profiled = false;
    LogLevel logLevel = LogLevel::INFO;
    bool printLogs = false;
};

} // namespace stablearb
