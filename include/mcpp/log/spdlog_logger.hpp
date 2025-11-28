#pragma once

#include "mcpp/log/logger.hpp"

#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// SpdlogLogger - Structured logging using spdlog
// ─────────────────────────────────────────────────────────────────────────────
// Provides structured logging with:
// - Multiple sinks (console, file, etc.)
// - Log rotation
// - Pattern formatting
// - Thread-safe async logging
// - JSON output support

class SpdlogLogger final : public ILogger {
public:
    /// Create logger with default console sink
    explicit SpdlogLogger(LogLevel min_level = LogLevel::Info);

    /// Create logger with custom spdlog logger
    explicit SpdlogLogger(std::shared_ptr<spdlog::logger> logger);

    /// Create logger with file sink
    SpdlogLogger(const std::string& filename, LogLevel min_level = LogLevel::Info);

    /// Create logger with multiple sinks
    SpdlogLogger(
        std::vector<spdlog::sink_ptr> sinks,
        LogLevel min_level = LogLevel::Info
    );

    ~SpdlogLogger() override = default;

    // Non-copyable, movable
    SpdlogLogger(const SpdlogLogger&) = delete;
    SpdlogLogger& operator=(const SpdlogLogger&) = delete;
    SpdlogLogger(SpdlogLogger&&) noexcept = default;
    SpdlogLogger& operator=(SpdlogLogger&&) noexcept = default;

    // ─────────────────────────────────────────────────────────────────────────
    // ILogger interface
    // ─────────────────────────────────────────────────────────────────────────

    void log(const LogRecord& record) override;

    [[nodiscard]] bool should_log(LogLevel level) const noexcept override;

    // ─────────────────────────────────────────────────────────────────────────
    // Spdlog-specific methods
    // ─────────────────────────────────────────────────────────────────────────

    /// Get underlying spdlog logger
    [[nodiscard]] std::shared_ptr<spdlog::logger> get_spdlog_logger() const noexcept {
        return logger_;
    }

    /// Set log level
    void set_level(LogLevel level) noexcept;

    /// Set pattern format (spdlog pattern syntax)
    void set_pattern(const std::string& pattern);

    /// Flush logs (useful for async logging)
    void flush();

    // ─────────────────────────────────────────────────────────────────────────
    // Static helpers (public for factory functions)
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] static spdlog::level::level_enum to_spdlog_level(LogLevel level) noexcept;
    [[nodiscard]] static LogLevel from_spdlog_level(spdlog::level::level_enum level) noexcept;

private:
    std::shared_ptr<spdlog::logger> logger_;
    LogLevel min_level_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Create a console logger with spdlog
[[nodiscard]] std::unique_ptr<SpdlogLogger> make_spdlog_console_logger(
    LogLevel min_level = LogLevel::Info
);

/// Create a file logger with spdlog
[[nodiscard]] std::unique_ptr<SpdlogLogger> make_spdlog_file_logger(
    const std::string& filename,
    LogLevel min_level = LogLevel::Info
);

/// Create a logger with both console and file sinks
[[nodiscard]] std::unique_ptr<SpdlogLogger> make_spdlog_console_file_logger(
    const std::string& filename,
    LogLevel min_level = LogLevel::Info
);

/// Create an async console logger (non-blocking, uses background thread)
[[nodiscard]] std::unique_ptr<SpdlogLogger> make_spdlog_async_console_logger(
    LogLevel min_level = LogLevel::Info,
    std::size_t queue_size = 8192,
    std::size_t thread_count = 1
);

/// Create an async file logger
[[nodiscard]] std::unique_ptr<SpdlogLogger> make_spdlog_async_file_logger(
    const std::string& filename,
    LogLevel min_level = LogLevel::Info,
    std::size_t queue_size = 8192,
    std::size_t thread_count = 1
);

}  // namespace mcpp

