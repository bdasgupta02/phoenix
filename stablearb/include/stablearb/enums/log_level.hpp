#pragma once

#include <boost/describe.hpp>

#include <iostream>
#include <string>

namespace stablearb {
BOOST_DEFINE_ENUM_CLASS(LogLevel, DEBUG, INFO, WARN, ERROR, FATAL)
}

namespace std {
std::istream& operator>>(std::istream& in, stablearb::LogLevel& logLevel)
{
    std::string token;
    in >> token;

    if (!boost::describe::enum_from_string<stablearb::LogLevel>(token.c_str(), logLevel))
        in.setstate(std::ios_base::failbit);

    return in;
}

std::ostream& operator<<(std::ostream& os, stablearb::LogLevel level)
{
    os << boost::describe::enum_to_string(level, 0);
    return os;
}
} // namespace std
