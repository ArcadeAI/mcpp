#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Log Levels
// ─────────────────────────────────────────────────────────────────────────────

enum class LogLevel : std::uint8_t {
    Trace = 0,  // Very detailed debugging
    Debug = 1,  // Debugging information
    Info  = 2,  // General information
    Warn  = 3,  // Warnings (recoverable issues)
    Error = 4,  // Errors (operation failed)
    Fatal = 5,  // Fatal errors (unrecoverable)
    Off   = 6   // Disable all logging
};

[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off:   return "OFF";
    }
    return "UNKNOWN";
}

// ─────────────────────────────────────────────────────────────────────────────
// Log Record - Immutable snapshot of a log event
// ─────────────────────────────────────────────────────────────────────────────

struct LogRecord {
    LogLevel level;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::source_location location;

    LogRecord(
        LogLevel lvl,
        std::string msg,
        std::source_location loc = std::source_location::current()
    )
        : level(lvl)
        , message(std::move(msg))
        , timestamp(std::chrono::system_clock::now())
        , location(loc)
    {}
};

// ─────────────────────────────────────────────────────────────────────────────
// ILogger Interface - Swappable logging backend
// ─────────────────────────────────────────────────────────────────────────────

class ILogger {
public:
    virtual ~ILogger() = default;

    // Core logging method
    virtual void log(const LogRecord& record) = 0;

    // Check if a level would be logged (for avoiding expensive formatting)
    [[nodiscard]] virtual bool should_log(LogLevel level) const noexcept = 0;

    // Convenience methods with source location capture
    void trace(std::string_view msg, std::source_location loc = std::source_location::current()) {
        if (should_log(LogLevel::Trace)) {
            log(LogRecord(LogLevel::Trace, std::string(msg), loc));
        }
    }

    void debug(std::string_view msg, std::source_location loc = std::source_location::current()) {
        if (should_log(LogLevel::Debug)) {
            log(LogRecord(LogLevel::Debug, std::string(msg), loc));
        }
    }

    void info(std::string_view msg, std::source_location loc = std::source_location::current()) {
        if (should_log(LogLevel::Info)) {
            log(LogRecord(LogLevel::Info, std::string(msg), loc));
        }
    }

    void warn(std::string_view msg, std::source_location loc = std::source_location::current()) {
        if (should_log(LogLevel::Warn)) {
            log(LogRecord(LogLevel::Warn, std::string(msg), loc));
        }
    }

    void error(std::string_view msg, std::source_location loc = std::source_location::current()) {
        if (should_log(LogLevel::Error)) {
            log(LogRecord(LogLevel::Error, std::string(msg), loc));
        }
    }

    void fatal(std::string_view msg, std::source_location loc = std::source_location::current()) {
        if (should_log(LogLevel::Fatal)) {
            log(LogRecord(LogLevel::Fatal, std::string(msg), loc));
        }
    }

    // Templated formatting helpers (C++20 std::format)
    template<typename... Args>
    void trace_fmt(std::format_string<Args...> fmt, Args&&... args) {
        if (should_log(LogLevel::Trace)) {
            log(LogRecord(LogLevel::Trace, std::format(fmt, std::forward<Args>(args)...)));
        }
    }

    template<typename... Args>
    void debug_fmt(std::format_string<Args...> fmt, Args&&... args) {
        if (should_log(LogLevel::Debug)) {
            log(LogRecord(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...)));
        }
    }

    template<typename... Args>
    void info_fmt(std::format_string<Args...> fmt, Args&&... args) {
        if (should_log(LogLevel::Info)) {
            log(LogRecord(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...)));
        }
    }

    template<typename... Args>
    void warn_fmt(std::format_string<Args...> fmt, Args&&... args) {
        if (should_log(LogLevel::Warn)) {
            log(LogRecord(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...)));
        }
    }

    template<typename... Args>
    void error_fmt(std::format_string<Args...> fmt, Args&&... args) {
        if (should_log(LogLevel::Error)) {
            log(LogRecord(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...)));
        }
    }

    template<typename... Args>
    void fatal_fmt(std::format_string<Args...> fmt, Args&&... args) {
        if (should_log(LogLevel::Fatal)) {
            log(LogRecord(LogLevel::Fatal, std::format(fmt, std::forward<Args>(args)...)));
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// NullLogger - Discards all logs (zero overhead when disabled)
// ─────────────────────────────────────────────────────────────────────────────

class NullLogger final : public ILogger {
public:
    void log(const LogRecord& /*record*/) override {
        // Intentionally empty - discards all logs
    }

    [[nodiscard]] bool should_log(LogLevel /*level*/) const noexcept override {
        return false;  // Never logs anything
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ConsoleLogger - Outputs to stderr with colors
// ─────────────────────────────────────────────────────────────────────────────

class ConsoleLogger final : public ILogger {
public:
    explicit ConsoleLogger(LogLevel min_level = LogLevel::Info)
        : min_level_(min_level)
    {}

    void log(const LogRecord& record) override;

    [[nodiscard]] bool should_log(LogLevel level) const noexcept override {
        return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(min_level_);
    }

    void set_level(LogLevel level) noexcept {
        min_level_ = level;
    }

    [[nodiscard]] LogLevel level() const noexcept {
        return min_level_;
    }

    // Enable/disable colors (auto-detected by default)
    void set_colors_enabled(bool enabled) noexcept {
        colors_enabled_ = enabled;
    }

private:
    LogLevel min_level_;
    bool colors_enabled_ = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Logger Access - Thread-safe singleton pattern
// ─────────────────────────────────────────────────────────────────────────────

// Get the global logger instance (defaults to NullLogger)
[[nodiscard]] ILogger& get_logger() noexcept;

// Set a new global logger (takes ownership)
void set_logger(std::unique_ptr<ILogger> logger) noexcept;

// Convenience macros for logging with automatic source location
// These check should_log() before evaluating arguments (zero overhead when disabled)

#define MCPP_LOG_TRACE(msg) \
    do { if (::mcpp::get_logger().should_log(::mcpp::LogLevel::Trace)) \
         ::mcpp::get_logger().trace(msg); } while(false)

#define MCPP_LOG_DEBUG(msg) \
    do { if (::mcpp::get_logger().should_log(::mcpp::LogLevel::Debug)) \
         ::mcpp::get_logger().debug(msg); } while(false)

#define MCPP_LOG_INFO(msg) \
    do { if (::mcpp::get_logger().should_log(::mcpp::LogLevel::Info)) \
         ::mcpp::get_logger().info(msg); } while(false)

#define MCPP_LOG_WARN(msg) \
    do { if (::mcpp::get_logger().should_log(::mcpp::LogLevel::Warn)) \
         ::mcpp::get_logger().warn(msg); } while(false)

#define MCPP_LOG_ERROR(msg) \
    do { if (::mcpp::get_logger().should_log(::mcpp::LogLevel::Error)) \
         ::mcpp::get_logger().error(msg); } while(false)

#define MCPP_LOG_FATAL(msg) \
    do { if (::mcpp::get_logger().should_log(::mcpp::LogLevel::Fatal)) \
         ::mcpp::get_logger().fatal(msg); } while(false)

}  // namespace mcpp

