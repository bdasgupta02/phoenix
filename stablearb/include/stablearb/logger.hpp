#pragma once

#include "stablearb/tags.hpp"

#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace stablearb {

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
} // namespace detail

struct Logger
{
    Logger() = default;
    Logger(Logger&& other) { assert(!other.running.test() && "Cannot move running logger"); }
    ~Logger() { shutdown(); }

    void handle(auto&, tag::Logger::Start, std::string_view appName)
    {
        ++LOGGERS;
        assert(LOGGERS == 1 && "Only one logger is allowed to run at a time at one time");
        running.test_and_set();

        auto now = std::chrono::system_clock::now();
        auto inTimeT = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << appName << "-";
        ss << std::put_time(std::gmtime(&inTimeT), "%Y%m%d-%H%M%S");
        ss << ".log";

        logPath = (std::filesystem::current_path() / ss.str()).string();
        logFile.emplace(std::ofstream(logPath, std::ios::app));
        logger.emplace(&Logger::loggerThread, this);
    }

    template<typename Tag, typename... Args>
    void handle(auto&, Tag, std::string_view filename, int line, bool print = false, Args&&... args)
    {
        handlerCache.str("");
        handlerCache.clear();
        ((handlerCache << " " << args), ...);

        Entry entry{.line = line, .print = print, .message = handlerCache.str(), .filename = std::string{filename}};

        if constexpr (std::is_same_v<Tag, tag::Logger::Info>)
            entry.level = Entry::Level::Info;
        else if constexpr (std::is_same_v<Tag, tag::Logger::Warn>)
            entry.level = Entry::Level::Warn;
        else if constexpr (std::is_same_v<Tag, tag::Logger::Error>)
            entry.level = Entry::Level::Error;
        else if constexpr (std::is_same_v<Tag, tag::Logger::Verify>)
            entry.level = Entry::Level::Verify;
        else
            static_assert(false, "Invalid tag passed to logger handler");

        while (!entries.push(std::move(entry)))
            ;
    }

    template<typename... Args>
    void handle(
        auto& graph,
        tag::Logger::Verify tag,
        bool condition,
        std::string_view filename,
        int line,
        bool print = false,
        Args&&... args)
    {
        if (!condition)
            handle(graph, tag, filename, line, print, std::forward<Args&&>(args)...);
    }

private:
    struct Entry
    {
        enum class Level
        {
            Info,
            Warn,
            Error,
            Verify
        };

        int line;
        bool print;
        Level level;
        std::string message;
        std::string filename;
    };

    void shutdown()
    {
        if (running.test())
        {
            running.clear();
            logger->join();
        }
    }

    void loggerThread()
    {
        auto const flushInterval = std::chrono::milliseconds(100);
        auto lastFlush = std::chrono::system_clock::now();
        Entry entry;

        auto const processEntry = [&entry, this]
        {
            std::string const formatted = formatEntry(entry);
            writeEntry(entry, formatted);
            if (entry.print)
                std::cout << formatted << std::endl;
        };

        while (running.test())
        {
            while (entries.pop(entry))
                processEntry();

            auto now = std::chrono::system_clock::now();
            if (now - lastFlush >= flushInterval)
            {
                logFile->flush();
                lastFlush = now;
            }

            std::this_thread::yield();
        }

        while (entries.pop(entry))
            processEntry();

        logFile->flush();
        logFile->close();
    }

    void writeEntry(Entry const& entry, std::string_view formatted) { *logFile << formatted << std::endl; }

    std::string formatEntry(Entry const& entry)
    {
        loggerCache.str("");
        loggerCache.clear();

        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);

        // clang-format off
        loggerCache
            << std::put_time(std::gmtime(&timeT), "%Y-%m-%d %H:%M:%S")
            << " "
            << entry.filename
            << ":" 
            << entry.line
            << " [" 
            << getLevelString(entry.level)
            << "] "
            << entry.message
        ;
        // clang-format on

        return loggerCache.str();
    }

    char const* getLevelString(Entry::Level level)
    {
        switch (level)
        {
        case Entry::Level::Info: return "INFO";
        case Entry::Level::Warn: return "WARN";
        case Entry::Level::Error: return "ERROR";
        case Entry::Level::Verify: return "VERIFY";
        default: return "UNKNOWN";
        }
    }

    static std::size_t LOGGERS;
    static constexpr std::size_t QUEUE_SIZE = 8192u;

    std::string logPath;
    std::optional<std::thread> logger;
    std::optional<std::ofstream> logFile;

    std::atomic_flag running = ATOMIC_FLAG_INIT;
    boost::lockfree::spsc_queue<Entry> entries{QUEUE_SIZE};

    std::stringstream handlerCache;
    std::stringstream loggerCache;
};

std::size_t Logger::LOGGERS = 0u;

// clang-format off
#define STABLEARB_LOG_INFO(graph, ...) \
    graph.invoke(tag::Logger::Info{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, false, __VA_ARGS__)
#define STABLEARB_LOG_INFO_PRINT(graph, ...) \
    graph.invoke(tag::Logger::Info{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, true, __VA_ARGS__)

#define STABLEARB_LOG_WARN(graph, ...) \
    graph.invoke(tag::Logger::Warn{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, false, __VA_ARGS__)
#define STABLEARB_LOG_WARN_PRINT(graph, ...) \
    graph.invoke(tag::Logger::Warn{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, true, __VA_ARGS__)

#define STABLEARB_LOG_ERROR(graph, ...) \
    graph.invoke(tag::Logger::Error{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, false, __VA_ARGS__)
#define STABLEARB_LOG_ERROR_PRINT(graph, ...) \
    graph.invoke(tag::Logger::Error{}, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, true, __VA_ARGS__)

#define STABLEARB_LOG_VERIFY(graph, condition, ...) \
    graph.invoke(tag::Logger::Verify{}, condition, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, false, __VA_ARGS__)
#define STABLEARB_LOG_VERIFY_PRINT(graph, condition, ...) \
    graph.invoke(tag::Logger::Verify{}, condition, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, true, __VA_ARGS__)
// clang-format on

} // namespace stablearb
