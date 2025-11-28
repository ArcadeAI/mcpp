#include "mcpp/log/logger.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// ANSI Color Codes
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr std::string_view RESET   = "\033[0m";
constexpr std::string_view GRAY    = "\033[90m";
constexpr std::string_view CYAN    = "\033[36m";
constexpr std::string_view GREEN   = "\033[32m";
constexpr std::string_view YELLOW  = "\033[33m";
constexpr std::string_view RED     = "\033[31m";
constexpr std::string_view MAGENTA = "\033[35m";
constexpr std::string_view BOLD    = "\033[1m";

[[nodiscard]] std::string_view level_color(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return GRAY;
        case LogLevel::Debug: return CYAN;
        case LogLevel::Info:  return GREEN;
        case LogLevel::Warn:  return YELLOW;
        case LogLevel::Error: return RED;
        case LogLevel::Fatal: return MAGENTA;
        case LogLevel::Off:   return RESET;
    }
    return RESET;
}

[[nodiscard]] std::string format_timestamp(
    const std::chrono::system_clock::time_point& tp
) {
    const auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()
    ).count() % 1000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_val);
#else
    localtime_r(&time_t_val, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms;
    return oss.str();
}

// Extract just the filename from a full path
[[nodiscard]] std::string_view extract_filename(const char* path) noexcept {
    std::string_view sv(path);
    const auto last_slash = sv.find_last_of("/\\");
    const bool found_slash = (last_slash != std::string_view::npos);
    if (found_slash) {
        return sv.substr(last_slash + 1);
    }
    return sv;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ConsoleLogger Implementation
// ─────────────────────────────────────────────────────────────────────────────

void ConsoleLogger::log(const LogRecord& record) {
    const bool should_log_this = should_log(record.level);
    if (should_log_this == false) {
        return;
    }

    // Thread-safe output
    static std::mutex output_mutex;
    std::lock_guard<std::mutex> lock(output_mutex);

    std::ostringstream oss;

    // Timestamp
    const bool use_colors = colors_enabled_;
    if (use_colors) {
        oss << GRAY;
    }
    oss << format_timestamp(record.timestamp);

    // Level
    if (use_colors) {
        oss << RESET << " " << BOLD << level_color(record.level);
    } else {
        oss << " ";
    }
    oss << std::setw(5) << std::left << to_string(record.level);
    if (use_colors) {
        oss << RESET;
    }

    // Source location (file:line)
    if (use_colors) {
        oss << " " << GRAY;
    } else {
        oss << " ";
    }
    oss << extract_filename(record.location.file_name())
        << ":" << record.location.line();
    if (use_colors) {
        oss << RESET;
    }

    // Message
    oss << " " << record.message << "\n";

    std::cerr << oss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Logger Singleton
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Default logger is NullLogger (zero overhead when not configured)
std::unique_ptr<ILogger>& logger_instance() {
    static std::unique_ptr<ILogger> instance = std::make_unique<NullLogger>();
    return instance;
}

std::mutex& logger_mutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

ILogger& get_logger() noexcept {
    std::lock_guard<std::mutex> lock(logger_mutex());
    return *logger_instance();
}

void set_logger(std::unique_ptr<ILogger> logger) noexcept {
    std::lock_guard<std::mutex> lock(logger_mutex());
    const bool is_valid = (logger != nullptr);
    if (is_valid) {
        logger_instance() = std::move(logger);
    } else {
        // Reset to NullLogger if nullptr passed
        logger_instance() = std::make_unique<NullLogger>();
    }
}

}  // namespace mcpp

