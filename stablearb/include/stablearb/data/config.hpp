#pragma once

#include "stablearb/enums/log_level.hpp"

#include <boost/describe/enum_from_string.hpp>
#include <boost/describe/enum_to_string.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace {
namespace po = boost::program_options;
}

namespace stablearb {

struct Config
{
    bool apply(int argc, char* argv[])
    {
        try
        {
            po::options_description desc(appName + " Arbitrage System Config");

            // clang-format off
            desc.add_options()
                ("help,h", "see all commands")
                ("username", po::value<std::string>(&username)->required(), "Deribit username")
                ("password", po::value<std::string>(&password)->required(), "Deribit password")
                ("host", po::value<std::string>(&password)->required(), "Deribit host address ([www/test].deribit.com for [prod/test])")
                ("port", po::value<std::string>(&password)->required(), "Deribit port (usually 9881 for TCP)")
                ("profiled", po::value<bool>(&profiled)->default_value(profiled), "Profiling mode")
                ("fee", po::value<double>(&fee)->default_value(fee), "Fee percentage")
                ("exit-ticks", po::value<int>(&exitTicks)->default_value(exitTicks), "Aggressive ticks to take profit and exit position")
                ("log-level", po::value<LogLevel>(&logLevel)->default_value(logLevel), "Log level [DEBUG, INFO, WARN, ERROR, FATAL]")
            ;

            if (appName == "") 
                desc.add_options()("appName", po::value<std::string>(&appName)->required(), "Application name");
            // clang-format on

            po::variables_map vm;
            po::store(po::parse_command_line(argc, argv, desc), vm);

            if (vm.count("help"))
            {
                std::cout << desc << std::endl;
                return false;
            }

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
    std::string host = "www.deribit.com"; // test.deribit.com:9881 for test net
    std::string port = "9881";

    // app
    std::string appName;
    bool profiled = false;
    double fee = 0.00;
    int exitTicks = 1;
    LogLevel logLevel = LogLevel::INFO;
};

} // namespace stablearb
