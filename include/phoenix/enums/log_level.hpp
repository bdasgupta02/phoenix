#pragma once

#include <boost/describe.hpp>

#include <iostream>
#include <string>

namespace phoenix {
BOOST_DEFINE_ENUM_CLASS(LogLevel, DEBUG, INFO, WARN, ERROR, FATAL)
char const* logLevelString(LogLevel level) { return boost::describe::enum_to_string(level, 0); }
} // namespace phoenix

namespace std {
std::istream& operator>>(std::istream& in, phoenix::LogLevel& logLevel)
{
    std::string token;
    in >> token;

    if (!boost::describe::enum_from_string<phoenix::LogLevel>(token.c_str(), logLevel))
        in.setstate(std::ios_base::failbit);

    return in;
}

std::ostream& operator<<(std::ostream& os, phoenix::LogLevel level)
{
    os << phoenix::logLevelString(level);
    return os;
}
} // namespace std
