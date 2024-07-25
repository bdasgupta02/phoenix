#pragma once

#include <boost/program_options/options_description.hpp>

#include <string>

namespace stablearb {

namespace po = boost::program_options;

struct Config
{
    // deribit connectivity
    std::string username;
    std::string password;

    // app
    std::string appName;
    bool sim = false;
    bool simProfiled = false;
    double fee = 0.00;
    int exitTicks = 1;
};

}; // namespace stablearb
