// ═══════════════════════════════════════════════════════════════════════════
// Concurrency Tests
// ═══════════════════════════════════════════════════════════════════════════
// Tests for thread safety and concurrent access to various components.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "mcpp/log/logger.hpp"
#include "mcpp/json/fast_json.hpp"
#include "mcpp/transport/sse_parser.hpp"
#include "mcpp/resilience/circuit_breaker.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <random>
#include <thread>
#include <vector>

using namespace mcpp;
using namespace std::chrono_literals;

// ═══════════════════════════════════════════════════════════════════════════
// Logger Concurrency Tests
// ═══════════════════════════════════════════════════════════════════════════

// Custom logger that counts log calls for testing
class CountingLogger : public ILogger {
public:
    void log(const LogRecord& /*record*/) override {
        log_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    [[nodiscard]] bool should_log(LogLevel /*level*/) const noexcept override {
        return true;
    }
    
    [[nodiscard]] std::size_t count() const noexcept {
        return log_count_.load(std::memory_order_relaxed);
    }
    
private:
    std::atomic<std::size_t> log_count_{0};
};

TEST_CASE("Logger atomic read path works correctly", "[logger][atomic]") {
    // Test that get_logger() returns a valid logger
    auto& logger = get_logger();
    
    // Default is NullLogger which returns false for should_log
    REQUIRE_FALSE(logger.should_log(LogLevel::Info));
    
    // Set a counting logger
    auto counting = std::make_unique<CountingLogger>();
    auto* ptr = counting.get();
    set_logger(std::move(counting));
    
    // Now should_log returns true
    REQUIRE(get_logger().should_log(LogLevel::Info));
    
    // Log something
    get_logger().info("test");
    REQUIRE(ptr->count() == 1);
    
    // Reset
    set_logger(nullptr);
    REQUIRE_FALSE(get_logger().should_log(LogLevel::Info));
}

TEST_CASE("Logger concurrent reads are safe", "[concurrency][logger]") {
    constexpr int num_threads = 4;
    constexpr int reads_per_thread = 1000;
    
    auto counting_logger = std::make_unique<CountingLogger>();
    set_logger(std::move(counting_logger));
    
    std::atomic<std::size_t> successful_reads{0};
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&successful_reads, reads_per_thread]() {
            for (int j = 0; j < reads_per_thread; ++j) {
                // Just read the logger - should never crash
                if (get_logger().should_log(LogLevel::Info)) {
                    successful_reads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(successful_reads.load() == num_threads * reads_per_thread);
    set_logger(nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// FastJsonParser Concurrency Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FastJsonParser thread-local instances are independent", "[concurrency][json]") {
    constexpr int num_threads = 8;
    constexpr int parses_per_thread = 100;
    
    std::atomic<std::size_t> success_count{0};
    std::atomic<std::size_t> error_count{0};
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, &success_count, &error_count, parses_per_thread]() {
            for (int j = 0; j < parses_per_thread; ++j) {
                // Each thread parses slightly different JSON
                std::string json = R"({"thread":)" + std::to_string(i) + 
                                  R"(,"iteration":)" + std::to_string(j) + "}";
                
                auto result = fast_parse(json);
                if (result.has_value()) {
                    // Verify the parsed values
                    if ((*result)["thread"].get<int>() == i &&
                        (*result)["iteration"].get<int>() == j) {
                        success_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        error_count.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(success_count.load() == num_threads * parses_per_thread);
    REQUIRE(error_count.load() == 0);
}

TEST_CASE("FastJsonParser depth limit is enforced", "[json][security]") {
    FastJsonParser parser(FastJsonConfig{.max_depth = 5});
    
    // Create deeply nested JSON
    std::string deep_json = R"({"a":{"b":{"c":{"d":{"e":{"f":"too deep"}}}}}})";
    auto result = parser.parse(deep_json);
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message.find("depth") != std::string::npos);
}

TEST_CASE("FastJsonParser errors are propagated correctly", "[json]") {
    FastJsonParser parser;
    
    SECTION("Invalid JSON") {
        auto result = parser.parse("{invalid}");
        REQUIRE_FALSE(result.has_value());
    }
    
    SECTION("Truncated JSON") {
        auto result = parser.parse("{\"key\":");
        REQUIRE_FALSE(result.has_value());
    }
    
    SECTION("Valid JSON") {
        auto result = parser.parse(R"({"key": "value"})");
        REQUIRE(result.has_value());
        REQUIRE((*result)["key"] == "value");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SSE Parser Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SseParser buffer limit is enforced", "[sse][security]") {
    SseParserConfig config;
    config.max_buffer_size = 1024;  // 1KB limit
    SseParser parser(config);
    
    // Feed data that exceeds the limit
    std::string large_chunk(2048, 'x');
    
    REQUIRE_THROWS_AS(parser.feed(large_chunk), SseBufferOverflowError);
}

TEST_CASE("SseParser event size limit discards oversized events", "[sse][security]") {
    SseParserConfig config;
    config.max_buffer_size = 1024 * 1024;  // 1MB buffer
    config.max_event_size = 100;  // 100 byte event limit
    SseParser parser(config);
    
    // Create a normal event first
    std::string normal_event = "data: small\n\n";
    auto events = parser.feed(normal_event);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "small");
    
    // Create an oversized event
    std::string large_data(200, 'x');
    std::string large_event = "data: " + large_data + "\n\n";
    events = parser.feed(large_event);
    REQUIRE(events.empty());  // Oversized event is discarded
    
    // Normal events still work after
    events = parser.feed(normal_event);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "small");
}

TEST_CASE("SseParser buffer_size returns current size", "[sse]") {
    SseParser parser;
    
    REQUIRE(parser.buffer_size() == 0);
    
    // Feed partial data (no newline)
    parser.feed("data: partial");
    REQUIRE(parser.buffer_size() > 0);
    
    // Complete the event
    auto events = parser.feed("\n\n");
    REQUIRE(events.size() == 1);
    
    // Buffer should be smaller after compaction
    // (or at least not growing unbounded)
}

// ═══════════════════════════════════════════════════════════════════════════
// Circuit Breaker Concurrency Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreaker stats are tracked correctly", "[circuit-breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 5;
    CircuitBreaker breaker(config);
    
    // Make some requests
    for (int i = 0; i < 10; ++i) {
        if (breaker.allow_request()) {
            if (i % 2 == 0) {
                breaker.record_success();
            } else {
                breaker.record_failure();
            }
        }
    }
    
    auto stats = breaker.stats();
    REQUIRE(stats.total_requests == 10);
    REQUIRE(stats.successful_requests + stats.failed_requests + stats.rejected_requests == 
            stats.total_requests);
}

TEST_CASE("CircuitBreaker state transitions work correctly", "[circuit-breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.recovery_timeout = 10ms;
    CircuitBreaker breaker(config);
    
    std::atomic<std::size_t> transitions{0};
    
    breaker.on_state_change([&transitions](CircuitState /*old*/, CircuitState /*new_state*/) {
        transitions.fetch_add(1, std::memory_order_relaxed);
    });
    
    // Cause failures to open circuit
    for (int i = 0; i < 5; ++i) {
        if (breaker.allow_request()) {
            breaker.record_failure();
        }
    }
    
    // Should have transitioned to open
    REQUIRE(breaker.state() == CircuitState::Open);
    REQUIRE(transitions.load() >= 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Handler Timeout Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("std::async with timeout works correctly", "[concurrency][timeout]") {
    SECTION("Fast operation completes before timeout") {
        auto future = std::async(std::launch::async, []() {
            return 42;
        });
        
        auto status = future.wait_for(100ms);
        REQUIRE(status == std::future_status::ready);
        REQUIRE(future.get() == 42);
    }
    
    SECTION("Slow operation times out") {
        auto future = std::async(std::launch::async, []() {
            std::this_thread::sleep_for(500ms);
            return 42;
        });
        
        auto status = future.wait_for(10ms);
        REQUIRE(status == std::future_status::timeout);
        
        // Still need to wait for the thread to complete
        // (in real code, we'd handle this differently)
        future.wait();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Memory Safety Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreaker can be used with shared_ptr", "[circuit-breaker][memory]") {
    auto breaker = std::make_shared<CircuitBreaker>();
    
    // Use it
    REQUIRE(breaker->allow_request());
    breaker->record_success();
    
    auto stats = breaker->stats();
    REQUIRE(stats.successful_requests == 1);
}

