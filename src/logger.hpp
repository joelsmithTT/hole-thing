#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <source_location>
#include <string_view>
#include <utility>

namespace logger {

enum class Level { DEBUG = 0, INFO, WARN, ERROR, FATAL };

inline std::atomic<Level> g_min_level = Level::DEBUG;
inline std::mutex g_log_mutex;

inline void set_min_level(Level level)
{
    g_min_level.store(level, std::memory_order_relaxed);
}

namespace detail {

inline const char* level_to_string(Level level)
{
    switch (level) {
    case Level::DEBUG:
        return "[debug]";
    case Level::INFO:
        return "[info ]";
    case Level::WARN:
        return "[warn ]";
    case Level::ERROR:
        return "[error]";
    case Level::FATAL:
        return "[fatal]";
    default:
        return "[?????]"; // Should not happen
    }
}

inline std::string_view extract_filename(std::string_view path)
{
    size_t last_separator = path.find_last_of("/\\");
    if (last_separator == std::string_view::npos) {
        return path;
    }
    return path.substr(last_separator + 1);
}

inline void format_timestamp(char* buffer, size_t buffer_size)
{
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm time_info;
    localtime_r(&now_c, &time_info); // POSIX standard

    size_t time_len = std::strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &time_info);

    if (time_len > 0 && buffer_size > time_len + 5) {
        std::snprintf(buffer + time_len, 5, ".%03lld", static_cast<long long>(now_ms.count()));
    } else if (time_len == 0) {
        std::snprintf(buffer, buffer_size, "TIMESTAMP_ERROR");
    }
}

template <typename Fmt, typename... Args> void log(Level level, std::source_location location, Fmt fmt, Args&&... args)
{
    if (level < g_min_level.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);

    char timestamp_buf[64]; // "[YYYY-MM-DD HH:MM:SS.ms]" is ~25 chars, give plenty of room
    format_timestamp(timestamp_buf, sizeof(timestamp_buf));

    std::string_view filename = extract_filename(location.file_name());

    std::fprintf(stderr, "[%s] %s [%.*s:%u] ", timestamp_buf, level_to_string(level),
                 static_cast<int>(filename.length()), filename.data(), location.line());

    if constexpr (sizeof...(Args) == 0) {
        std::fputs(fmt, stderr);
    } else {
        std::fprintf(stderr, fmt, std::forward<Args>(args)...);
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr); // Ensure the message is written out immediately

    if (level == Level::FATAL) {
        std::terminate();
    }
}

} // namespace detail

#define LOG_DEBUG(fmt, ...)                                                                                            \
    ::logger::detail::log(::logger::Level::DEBUG, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                                                                                             \
    ::logger::detail::log(::logger::Level::INFO, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)                                                                                             \
    ::logger::detail::log(::logger::Level::WARN, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)                                                                                            \
    ::logger::detail::log(::logger::Level::ERROR, std::source_location::current(), fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)                                                                                            \
    ::logger::detail::log(::logger::Level::FATAL, std::source_location::current(), fmt, ##__VA_ARGS__)

#define RUNTIME_ERROR(msg, ...)                                                                                        \
    do {                                                                                                               \
        LOG_ERROR(msg, ##__VA_ARGS__);                                                                                 \
        throw std::runtime_error(msg);                                                                                 \
    } while (0)

#define SYSTEM_ERROR(msg, ...)                                                                                         \
    do {                                                                                                               \
        LOG_ERROR(msg ": %s", ##__VA_ARGS__, std::strerror(errno));                                                    \
        throw std::system_error(errno, std::generic_category(), msg);                                                  \
    } while (0)

} // namespace logger