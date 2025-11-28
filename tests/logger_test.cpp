#include <catch2/catch_test_macros.hpp>

#include "mcpp/log/logger.hpp"

#include <sstream>
#include <vector>

using namespace mcpp;

// ─────────────────────────────────────────────────────────────────────────────
// Test Logger - Captures log messages for verification
// ─────────────────────────────────────────────────────────────────────────────

class TestLogger final : public ILogger {
public:
    explicit TestLogger(LogLevel min_level = LogLevel::Trace)
        : min_level_(min_level)
    {}

    void log(const LogRecord& record) override {
        records_.push_back(record);
    }

    [[nodiscard]] bool should_log(LogLevel level) const noexcept override {
        return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(min_level_);
    }

    [[nodiscard]] const std::vector<LogRecord>& records() const noexcept {
        return records_;
    }

    void clear() {
        records_.clear();
    }

private:
    LogLevel min_level_;
    std::vector<LogRecord> records_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("LogLevel to_string returns correct names", "[log]") {
    REQUIRE(to_string(LogLevel::Trace) == "TRACE");
    REQUIRE(to_string(LogLevel::Debug) == "DEBUG");
    REQUIRE(to_string(LogLevel::Info) == "INFO");
    REQUIRE(to_string(LogLevel::Warn) == "WARN");
    REQUIRE(to_string(LogLevel::Error) == "ERROR");
    REQUIRE(to_string(LogLevel::Fatal) == "FATAL");
    REQUIRE(to_string(LogLevel::Off) == "OFF");
}

TEST_CASE("NullLogger discards all messages", "[log]") {
    NullLogger logger;

    REQUIRE(logger.should_log(LogLevel::Trace) == false);
    REQUIRE(logger.should_log(LogLevel::Fatal) == false);

    // Should not crash
    logger.trace("test");
    logger.debug("test");
    logger.info("test");
    logger.warn("test");
    logger.error("test");
    logger.fatal("test");
}

TEST_CASE("TestLogger captures messages at or above min level", "[log]") {
    TestLogger logger(LogLevel::Warn);

    logger.trace("trace message");
    logger.debug("debug message");
    logger.info("info message");
    logger.warn("warn message");
    logger.error("error message");

    // Only Warn and above should be captured
    REQUIRE(logger.records().size() == 2);
    REQUIRE(logger.records()[0].level == LogLevel::Warn);
    REQUIRE(logger.records()[0].message == "warn message");
    REQUIRE(logger.records()[1].level == LogLevel::Error);
    REQUIRE(logger.records()[1].message == "error message");
}

TEST_CASE("LogRecord captures source location", "[log]") {
    TestLogger logger;
    logger.info("test message");

    REQUIRE(logger.records().size() == 1);
    const auto& record = logger.records()[0];

    // Source location should be captured
    std::string_view filename(record.location.file_name());
    const bool has_test_filename = (filename.find("logger_test") != std::string_view::npos);
    REQUIRE(has_test_filename);
    REQUIRE(record.location.line() > 0);
}

TEST_CASE("LogRecord captures timestamp", "[log]") {
    TestLogger logger;

    auto before = std::chrono::system_clock::now();
    logger.info("test message");
    auto after = std::chrono::system_clock::now();

    REQUIRE(logger.records().size() == 1);
    const auto& record = logger.records()[0];

    REQUIRE(record.timestamp >= before);
    REQUIRE(record.timestamp <= after);
}

TEST_CASE("ConsoleLogger respects min level", "[log]") {
    ConsoleLogger logger(LogLevel::Error);

    REQUIRE(logger.should_log(LogLevel::Trace) == false);
    REQUIRE(logger.should_log(LogLevel::Debug) == false);
    REQUIRE(logger.should_log(LogLevel::Info) == false);
    REQUIRE(logger.should_log(LogLevel::Warn) == false);
    REQUIRE(logger.should_log(LogLevel::Error) == true);
    REQUIRE(logger.should_log(LogLevel::Fatal) == true);
}

TEST_CASE("ConsoleLogger level can be changed", "[log]") {
    ConsoleLogger logger(LogLevel::Error);

    REQUIRE(logger.level() == LogLevel::Error);
    REQUIRE(logger.should_log(LogLevel::Warn) == false);

    logger.set_level(LogLevel::Warn);

    REQUIRE(logger.level() == LogLevel::Warn);
    REQUIRE(logger.should_log(LogLevel::Warn) == true);
}

TEST_CASE("Global logger defaults to NullLogger", "[log]") {
    // Reset to default
    set_logger(nullptr);

    // Default should be NullLogger (nothing logged)
    REQUIRE(get_logger().should_log(LogLevel::Fatal) == false);
}

TEST_CASE("Global logger can be swapped", "[log]") {
    auto test_logger = std::make_unique<TestLogger>();
    auto* raw_ptr = test_logger.get();

    set_logger(std::move(test_logger));

    get_logger().info("test message");

    // Verify the message was captured
    REQUIRE(raw_ptr->records().size() == 1);
    REQUIRE(raw_ptr->records()[0].message == "test message");

    // Reset to default
    set_logger(nullptr);
}

TEST_CASE("MCPP_LOG macros work correctly", "[log]") {
    auto test_logger = std::make_unique<TestLogger>(LogLevel::Debug);
    auto* raw_ptr = test_logger.get();

    set_logger(std::move(test_logger));

    MCPP_LOG_TRACE("trace");  // Should be filtered
    MCPP_LOG_DEBUG("debug");
    MCPP_LOG_INFO("info");
    MCPP_LOG_WARN("warn");
    MCPP_LOG_ERROR("error");
    MCPP_LOG_FATAL("fatal");

    // Trace should be filtered (min level is Debug)
    REQUIRE(raw_ptr->records().size() == 5);
    REQUIRE(raw_ptr->records()[0].level == LogLevel::Debug);
    REQUIRE(raw_ptr->records()[1].level == LogLevel::Info);
    REQUIRE(raw_ptr->records()[2].level == LogLevel::Warn);
    REQUIRE(raw_ptr->records()[3].level == LogLevel::Error);
    REQUIRE(raw_ptr->records()[4].level == LogLevel::Fatal);

    // Reset to default
    set_logger(nullptr);
}

