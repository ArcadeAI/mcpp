// ─────────────────────────────────────────────────────────────────────────────
// SpdlogLogger Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for structured logging using spdlog

#include <catch2/catch_test_macros.hpp>

#include "mcpp/log/spdlog_logger.hpp"
#include "mcpp/log/logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace mcpp;

// ═══════════════════════════════════════════════════════════════════════════
// Basic Functionality Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpdlogLogger can be created with console sink", "[log][spdlog]") {
    auto logger = make_spdlog_console_logger(LogLevel::Debug);
    REQUIRE(logger != nullptr);
    
    REQUIRE(logger->should_log(LogLevel::Debug));
    REQUIRE(logger->should_log(LogLevel::Info));
    REQUIRE_FALSE(logger->should_log(LogLevel::Trace));
}

TEST_CASE("SpdlogLogger respects minimum log level", "[log][spdlog]") {
    auto logger = make_spdlog_console_logger(LogLevel::Warn);
    
    REQUIRE_FALSE(logger->should_log(LogLevel::Trace));
    REQUIRE_FALSE(logger->should_log(LogLevel::Debug));
    REQUIRE_FALSE(logger->should_log(LogLevel::Info));
    REQUIRE(logger->should_log(LogLevel::Warn));
    REQUIRE(logger->should_log(LogLevel::Error));
    REQUIRE(logger->should_log(LogLevel::Fatal));
}

TEST_CASE("SpdlogLogger can change log level", "[log][spdlog]") {
    auto logger = make_spdlog_console_logger(LogLevel::Info);
    
    REQUIRE(logger->should_log(LogLevel::Info));
    REQUIRE_FALSE(logger->should_log(LogLevel::Debug));
    
    logger->set_level(LogLevel::Debug);
    
    REQUIRE(logger->should_log(LogLevel::Debug));
    REQUIRE(logger->should_log(LogLevel::Info));
}

TEST_CASE("SpdlogLogger can log messages", "[log][spdlog]") {
    auto logger = make_spdlog_console_logger(LogLevel::Trace);
    
    // Should not throw
    logger->trace("Trace message");
    logger->debug("Debug message");
    logger->info("Info message");
    logger->warn("Warning message");
    logger->error("Error message");
    logger->fatal("Fatal message");
}

TEST_CASE("SpdlogLogger can log formatted messages", "[log][spdlog]") {
    auto logger = make_spdlog_console_logger(LogLevel::Debug);
    
    // Should not throw
    logger->debug_fmt("Debug: {} {}", 42, "test");
    logger->info_fmt("Info: {}", "formatted");
    logger->warn_fmt("Warning: count={}", 100);
}

// ═══════════════════════════════════════════════════════════════════════════
// File Logging Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpdlogLogger can log to file", "[log][spdlog][file]") {
    const std::string test_file = "test_spdlog.log";
    
    // Clean up any existing file
    std::filesystem::remove(test_file);
    
    {
        auto logger = make_spdlog_file_logger(test_file, LogLevel::Info);
        
        logger->info("Test message to file");
        logger->warn("Warning message");
        // Flush using SpdlogLogger method
        static_cast<SpdlogLogger*>(logger.get())->flush();
    }
    
    // Verify file was created and contains messages
    REQUIRE(std::filesystem::exists(test_file));
    
    std::ifstream file(test_file);
    REQUIRE(file.is_open());
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    REQUIRE(content.find("Test message to file") != std::string::npos);
    REQUIRE(content.find("Warning message") != std::string::npos);
    
    // Cleanup
    std::filesystem::remove(test_file);
}

TEST_CASE("SpdlogLogger file logger respects log level", "[log][spdlog][file]") {
    const std::string test_file = "test_spdlog_level.log";
    std::filesystem::remove(test_file);
    
    {
        auto logger = make_spdlog_file_logger(test_file, LogLevel::Warn);
        
        logger->debug("This should not appear");
        logger->info("This should not appear");
        logger->warn("This should appear");
        logger->error("This should appear");
        static_cast<SpdlogLogger*>(logger.get())->flush();
    }
    
    std::ifstream file(test_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    REQUIRE(content.find("This should not appear") == std::string::npos);
    REQUIRE(content.find("This should appear") != std::string::npos);
    
    std::filesystem::remove(test_file);
}

// ═══════════════════════════════════════════════════════════════════════════
// Async Logging Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpdlogLogger async console logger works", "[log][spdlog][async]") {
    auto logger = make_spdlog_async_console_logger(LogLevel::Info);
    REQUIRE(logger != nullptr);
    
    // Log multiple messages
    for (int i = 0; i < 10; ++i) {
        logger->info_fmt("Async message {}", i);
    }
    
    // Flush to ensure messages are processed
    static_cast<SpdlogLogger*>(logger.get())->flush();
    
    REQUIRE(logger->should_log(LogLevel::Info));
}

TEST_CASE("SpdlogLogger async file logger works", "[log][spdlog][async]") {
    const std::string test_file = "test_spdlog_async.log";
    std::filesystem::remove(test_file);
    
    {
        auto logger = make_spdlog_async_file_logger(test_file, LogLevel::Debug);
        
        // Log multiple messages asynchronously
        for (int i = 0; i < 20; ++i) {
            logger->debug_fmt("Async file message {}", i);
        }
        
        // Flush to ensure all messages are written
        static_cast<SpdlogLogger*>(logger.get())->flush();
        
        // Small delay to ensure async writes complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Verify file contains messages
    REQUIRE(std::filesystem::exists(test_file));
    
    std::ifstream file(test_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    REQUIRE(content.find("Async file message") != std::string::npos);
    REQUIRE(content.find("Async file message 0") != std::string::npos);
    REQUIRE(content.find("Async file message 19") != std::string::npos);
    
    std::filesystem::remove(test_file);
}

// ═══════════════════════════════════════════════════════════════════════════
// Pattern Formatting Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpdlogLogger can set custom pattern", "[log][spdlog][pattern]") {
    const std::string test_file = "test_spdlog_pattern.log";
    std::filesystem::remove(test_file);
    
    {
        auto logger = make_spdlog_file_logger(test_file, LogLevel::Info);
        logger->set_pattern("%v");  // Only message, no timestamp/level
        logger->info("Simple message");
        static_cast<SpdlogLogger*>(logger.get())->flush();
    }
    
    std::ifstream file(test_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    REQUIRE(content.find("Simple message") != std::string::npos);
    
    std::filesystem::remove(test_file);
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration with Global Logger
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SpdlogLogger can be set as global logger", "[log][spdlog][integration]") {
    const std::string test_file = "test_spdlog_global.log";
    std::filesystem::remove(test_file);
    
    {
        auto logger = make_spdlog_file_logger(test_file, LogLevel::Info);
        auto* spdlog_ptr = static_cast<SpdlogLogger*>(logger.get());
        set_logger(std::move(logger));
        
        // Use global logger
        get_logger().info("Global logger test");
        
        // Flush to ensure message is written before reading
        spdlog_ptr->flush();
    }
    
    REQUIRE(std::filesystem::exists(test_file));
    
    std::ifstream file(test_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    REQUIRE(content.find("Global logger test") != std::string::npos);
    
    // Reset to null logger
    set_logger(nullptr);
    
    std::filesystem::remove(test_file);
}

