#include "mcpp/log/spdlog_logger.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>

namespace mcpp {

namespace {
    // Generate unique logger names to avoid conflicts in spdlog's global registry
    std::string generate_unique_logger_name(const std::string& base_name) {
        static std::atomic<uint64_t> counter{0};
        return base_name + "_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
    }
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Level Conversion
// ─────────────────────────────────────────────────────────────────────────────

spdlog::level::level_enum SpdlogLogger::to_spdlog_level(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return spdlog::level::trace;
        case LogLevel::Debug: return spdlog::level::debug;
        case LogLevel::Info:  return spdlog::level::info;
        case LogLevel::Warn:  return spdlog::level::warn;
        case LogLevel::Error: return spdlog::level::err;
        case LogLevel::Fatal: return spdlog::level::critical;
        case LogLevel::Off:   return spdlog::level::off;
    }
    return spdlog::level::info;
}

LogLevel SpdlogLogger::from_spdlog_level(spdlog::level::level_enum level) noexcept {
    switch (level) {
        case spdlog::level::trace:    return LogLevel::Trace;
        case spdlog::level::debug:    return LogLevel::Debug;
        case spdlog::level::info:     return LogLevel::Info;
        case spdlog::level::warn:     return LogLevel::Warn;
        case spdlog::level::err:      return LogLevel::Error;
        case spdlog::level::critical: return LogLevel::Fatal;
        case spdlog::level::off:      return LogLevel::Off;
        default:                      return LogLevel::Info;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

SpdlogLogger::SpdlogLogger(LogLevel min_level)
    : logger_(spdlog::stdout_color_mt(generate_unique_logger_name("mcpp")))
    , min_level_(min_level)
{
    logger_->set_level(to_spdlog_level(min_level));
    // Default pattern: [timestamp] [level] [file:line] message
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
}

SpdlogLogger::SpdlogLogger(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger))
    , min_level_(from_spdlog_level(logger_->level()))
{
    if (!logger_) {
        throw std::invalid_argument("SpdlogLogger: logger cannot be null");
    }
}

SpdlogLogger::SpdlogLogger(const std::string& filename, LogLevel min_level)
    : logger_(std::make_shared<spdlog::logger>(
          generate_unique_logger_name("mcpp_file"),
          std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename)
      ))
    , min_level_(min_level)
{
    logger_->set_level(to_spdlog_level(min_level));
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    // Note: We don't register with spdlog's global registry to avoid name conflicts
}

SpdlogLogger::SpdlogLogger(
    std::vector<spdlog::sink_ptr> sinks,
    LogLevel min_level
)
    : logger_(std::make_shared<spdlog::logger>(
          generate_unique_logger_name("mcpp_multi"),
          sinks.begin(),
          sinks.end()
      ))
    , min_level_(min_level)
{
    logger_->set_level(to_spdlog_level(min_level));
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    // Note: We don't register with spdlog's global registry to avoid name conflicts
}

// ─────────────────────────────────────────────────────────────────────────────
// ILogger Implementation
// ─────────────────────────────────────────────────────────────────────────────

void SpdlogLogger::log(const LogRecord& record) {
    if (!should_log(record.level)) {
        return;
    }

    const auto spdlog_level = to_spdlog_level(record.level);

    // Format message with source location
    // spdlog will handle the pattern formatting
    logger_->log(
        spdlog::source_loc{
            record.location.file_name(),
            static_cast<int>(record.location.line()),
            record.location.function_name()
        },
        spdlog_level,
        "{}",
        record.message
    );
}

bool SpdlogLogger::should_log(LogLevel level) const noexcept {
    return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(min_level_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Spdlog-specific Methods
// ─────────────────────────────────────────────────────────────────────────────

void SpdlogLogger::set_level(LogLevel level) noexcept {
    min_level_ = level;
    logger_->set_level(to_spdlog_level(level));
}

void SpdlogLogger::set_pattern(const std::string& pattern) {
    logger_->set_pattern(pattern);
}

void SpdlogLogger::flush() {
    logger_->flush();
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory Functions
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<SpdlogLogger> make_spdlog_console_logger(LogLevel min_level) {
    return std::make_unique<SpdlogLogger>(min_level);
}

std::unique_ptr<SpdlogLogger> make_spdlog_file_logger(
    const std::string& filename,
    LogLevel min_level
) {
    return std::make_unique<SpdlogLogger>(filename, min_level);
}

std::unique_ptr<SpdlogLogger> make_spdlog_console_file_logger(
    const std::string& filename,
    LogLevel min_level
) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename));
    return std::make_unique<SpdlogLogger>(std::move(sinks), min_level);
}

namespace {
    // Shared thread pool for async logging (initialized once)
    std::shared_ptr<spdlog::details::thread_pool> get_async_thread_pool(std::size_t queue_size, std::size_t thread_count) {
        static std::once_flag async_init_flag;
        static std::shared_ptr<spdlog::details::thread_pool> thread_pool;
        std::call_once(async_init_flag, [queue_size, thread_count]() {
            thread_pool = std::make_shared<spdlog::details::thread_pool>(queue_size, thread_count);
        });
        return thread_pool;
    }
}  // namespace

std::unique_ptr<SpdlogLogger> make_spdlog_async_console_logger(
    LogLevel min_level,
    std::size_t queue_size,
    std::size_t thread_count
) {
    auto thread_pool = get_async_thread_pool(queue_size, thread_count);
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    
    // async_logger needs weak_ptr to thread_pool
    auto logger = std::shared_ptr<spdlog::async_logger>(
        new spdlog::async_logger(
            generate_unique_logger_name("mcpp_async"),
            sink,
            thread_pool,
            spdlog::async_overflow_policy::block
        )
    );
    logger->set_level(SpdlogLogger::to_spdlog_level(min_level));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    // Note: We don't register with spdlog's global registry to avoid name conflicts
    
    return std::make_unique<SpdlogLogger>(std::move(logger));
}

std::unique_ptr<SpdlogLogger> make_spdlog_async_file_logger(
    const std::string& filename,
    LogLevel min_level,
    std::size_t queue_size,
    std::size_t thread_count
) {
    auto thread_pool = get_async_thread_pool(queue_size, thread_count);
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename);
    
    // async_logger needs weak_ptr to thread_pool
    auto logger = std::shared_ptr<spdlog::async_logger>(
        new spdlog::async_logger(
            generate_unique_logger_name("mcpp_async_file"),
            sink,
            thread_pool,
            spdlog::async_overflow_policy::block
        )
    );
    logger->set_level(SpdlogLogger::to_spdlog_level(min_level));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    // Note: We don't register with spdlog's global registry to avoid name conflicts
    
    return std::make_unique<SpdlogLogger>(std::move(logger));
}

}  // namespace mcpp

