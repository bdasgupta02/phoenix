#pragma once

#include "stablearb/data/config.hpp"
#include "stablearb/enums/log_level.hpp"
#include "stablearb/graph/node_base.hpp"
#include "stablearb/tags.hpp"

#include <boost/describe.hpp>
#include <boost/lockfree/spsc_queue.hpp>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

#include <immintrin.h>

namespace stablearb {

namespace detail {
consteval std::string_view getFilename(std::string_view path)
{
    auto lastSlash = path.find_last_of("/\\");
    if (lastSlash == std::string_view::npos)
        return path;

    return path.substr(lastSlash + 1);
}

#define STABLEARB_LOG_DETAIL_CURRENT_FILE ::stablearb::detail::getFilename(__FILE__)
} // namespace detail

// clang-format off
#define STABLEARB_LOG_DEBUG(handler, ...) \
    handler->invoke(tag::Logger::Log{}, LogLevel::DEBUG, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, __VA_ARGS__)
#define STABLEARB_LOG_INFO(handler, ...) \
    handler->invoke(tag::Logger::Log{}, LogLevel::INFO, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, __VA_ARGS__)
#define STABLEARB_LOG_WARN(handler, ...) \
    handler->invoke(tag::Logger::Log{}, LogLevel::WARN, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, __VA_ARGS__)
#define STABLEARB_LOG_ERROR(handler, ...) \
    handler->invoke(tag::Logger::Log{}, LogLevel::ERROR, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, __VA_ARGS__)
#define STABLEARB_LOG_FATAL(handler, ...) \
    handler->invoke(tag::Logger::Log{}, LogLevel::FATAL, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, __VA_ARGS__)
#define STABLEARB_LOG_VERIFY(handler, condition, ...) \
    handler->invoke(tag::Logger::Verify{}, condition, STABLEARB_LOG_DETAIL_CURRENT_FILE, __LINE__, __VA_ARGS__)
// clang-format on

template<typename NodeBase>
struct Logger : NodeBase
{
    template<typename Traits, typename Router>
    Logger(Config<Traits> const& config, RouterHandler<Router>& handler)
        : NodeBase{config, handler}
        , logLevel{config.logLevel}
        , appName{config.appName}
    {}

    Logger(Logger&& other) { assert(!other.running.test() && "Cannot move running logger"); }

    ~Logger()
    {
        shutdown();
        logger->join();
    }

    void handle(tag::Logger::Start)
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

    template<typename... Args>
    inline void handle(
        tag::Logger::Log, LogLevel level, std::string_view filename, int line, Args&&... args)
    {
        if (level < logLevel)
            return;

        handlerCache.str("");
        handlerCache.clear();
        ((handlerCache << " " << args), ...);

        Entry entry{
            .line = line,
            .level = level,
            .message = handlerCache.str(),
            .filename = std::string{filename}};

        while (!entries.push(entry))
            _mm_pause();
    }

    template<typename... Args>
    inline void handle(
        tag::Logger::Verify, bool condition, std::string_view filename, int line, Args&&... args)
    {
        if (!condition)
            handle(tag::Logger::Log{}, LogLevel::FATAL, filename, line, std::forward<Args&&>(args)...);
    }

    void handle(tag::Logger::Stop)
    {
        shutdown();
        logger->join();
    }

private:
    struct Entry
    {
        int line;
        LogLevel level;
        std::string message;
        std::string filename;
    };

    void shutdown()
    {
        if (running.test())
        {
            running.clear();
            --LOGGERS;
        }
    }

    void loggerThread()
    {
        auto const flushInterval = std::chrono::milliseconds(100);
        auto lastFlush = std::chrono::system_clock::now();
        Entry entry;

        auto* config = this->getConfig();
        auto const processEntry = [&entry, config, this]
        {
            std::string const formatted = formatEntry(entry);
            writeEntry(formatted);

            if (config->printLogs)
                std::cout << formatted << std::endl;

            if (entry.level == LogLevel::FATAL)
            {
                logFile->flush();
                logFile->close();
                running.clear();
                this->getHandler()->invoke(tag::Risk::Abort{});
            }
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

    void writeEntry(std::string_view formatted) { *logFile << formatted << std::endl; }

    std::string formatEntry(Entry const& entry)
    {
        loggerCache.str("");
        loggerCache.clear();

        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);

        // clang-format off
        loggerCache
            << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%S")
            << " "
            << entry.filename
            << ":" 
            << entry.line
            << " [" 
            << logLevelString(entry.level)
            << "]"
            << entry.message
        ;
        // clang-format on

        return loggerCache.str();
    }

    static std::size_t LOGGERS;
    static constexpr std::size_t QUEUE_SIZE = 8192u;

    LogLevel logLevel;
    std::string appName;
    std::string logPath;
    std::optional<std::thread> logger;
    std::optional<std::ofstream> logFile;

    std::atomic_flag running = ATOMIC_FLAG_INIT;
    boost::lockfree::spsc_queue<Entry> entries{QUEUE_SIZE};

    std::stringstream handlerCache;
    std::stringstream loggerCache;
};

template<typename NodeBase>
std::size_t Logger<NodeBase>::LOGGERS = 0u;

} // namespace stablearb
