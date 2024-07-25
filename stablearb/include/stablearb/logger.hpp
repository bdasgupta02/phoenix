#pragma once

#include "stablearb/tags.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace stablearb {

struct Logger
{
    Logger()
    {
        // open file and start schedule on new thread
        // open such that grep or cat in the background still works perfectly even with the file open
        // flush every second or so? try not to flush incomplete lines
    }

    ~Logger()
    {
        logger.join();
        // close file
    }

    Logger(Logger&& other) = default;

    template<typename Tag>
    void handle(auto&, Tag, std::string_view filename, int line, bool print = false, auto&&... args)
    {
        // for Tag = tag::Logging::Error: after logging (and maybe printing), terminate
    }

    void handle(
        auto&,
        tag::Logger::Verify,
        bool condition,
        std::string_view filename,
        int line,
        bool print = false,
        auto&&... args)
    {
        // after logging (and maybe printing), terminate
    }

private:
    void loggerThread() {}

    std::thread logger{&Logger::loggerThread, this};
};

namespace detail {
consteval std::string_view getFilename(std::string_view path)
{
    auto lastSlash = path.find_last_of("/\\");
    if (lastSlash == std::string_view::npos)
    {
        return path;
    }
    return path.substr(lastSlash + 1);
}

#define STABLEARB_LOG_DETAIL_CURRENT_FILE ::stablearb::detail::getFilename(__FILE__)
#define STABLEARB_LOG_DETAIL_LINE_NUMBER __LINE__
} // namespace detail

// clang-format off
#define STABLEARB_LOG_INFO(graph, ...) \
    graph.invoke(tag::Logger::Info{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, false, __VA_ARGS__)
#define STABLEARB_LOG_INFO_PRINT(graph, ...) \
    graph.invoke(tag::Logger::Info{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, true, __VA_ARGS__)

#define STABLEARB_LOG_WARN(graph, ...) \
    graph.invoke(tag::Logger::Warn{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, false, __VA_ARGS__)
#define STABLEARB_LOG_WARN_PRINT(graph, ...) \
    graph.invoke(tag::Logger::Warn{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, true, __VA_ARGS__)

#define STABLEARB_LOG_ERROR(graph, ...) \
    graph.invoke(tag::Logger::Error{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, false, __VA_ARGS__)
#define STABLEARB_LOG_ERROR_PRINT(graph, ...) \
    graph.invoke(tag::Logger::Error{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, true, __VA_ARGS__)

#define STABLEARB_LOG_VERIFY(graph, condition, ...) \
    graph.invoke(tag::Logger::Verify{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, false, __VA_ARGS__)
#define STABLEARB_LOG_VERIFY_PRINT(graph, condition, ...) \
    graph.invoke(tag::Logger::Verify{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, STABLEARB_LOG_DETAIL_LINE_NUMBER, true, __VA_ARGS__)
// clang-format on

} // namespace stablearb
