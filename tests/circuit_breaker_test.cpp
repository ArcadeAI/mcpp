// ─────────────────────────────────────────────────────────────────────────────
// Circuit Breaker Tests
// ─────────────────────────────────────────────────────────────────────────────

#include <catch2/catch_test_macros.hpp>

#include "mcpp/resilience/circuit_breaker.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace mcpp;
using namespace std::chrono_literals;

// ═══════════════════════════════════════════════════════════════════════════
// Basic State Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreaker starts in closed state", "[resilience][circuit_breaker]") {
    CircuitBreaker breaker;
    
    REQUIRE(breaker.state() == CircuitState::Closed);
    REQUIRE(breaker.is_closed());
    REQUIRE_FALSE(breaker.is_open());
}

TEST_CASE("CircuitBreaker allows requests when closed", "[resilience][circuit_breaker]") {
    CircuitBreaker breaker;
    
    REQUIRE(breaker.allow_request());
    REQUIRE(breaker.allow_request());
    REQUIRE(breaker.allow_request());
}

TEST_CASE("CircuitBreaker opens after failure threshold", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    CircuitBreaker breaker(config);
    
    // First two failures - still closed
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_closed());
    
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_closed());
    
    // Third failure - opens
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_open());
}

TEST_CASE("CircuitBreaker rejects requests when open", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 1h;  // Long timeout to stay open
    CircuitBreaker breaker(config);
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_open());
    
    // Requests should be rejected
    REQUIRE_FALSE(breaker.allow_request());
    REQUIRE_FALSE(breaker.allow_request());
    
    auto stats = breaker.stats();
    REQUIRE(stats.rejected_requests == 2);
}

TEST_CASE("CircuitBreaker success resets failure count", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    CircuitBreaker breaker(config);
    
    // Two failures
    breaker.allow_request();
    breaker.record_failure();
    breaker.allow_request();
    breaker.record_failure();
    
    // Success resets count
    breaker.allow_request();
    breaker.record_success();
    
    // Two more failures - still closed (count was reset)
    breaker.allow_request();
    breaker.record_failure();
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_closed());
    
    // Third failure - now opens
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_open());
}

// ═══════════════════════════════════════════════════════════════════════════
// Recovery Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreaker transitions to half-open after timeout", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 10ms;
    CircuitBreaker breaker(config);
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_open());
    
    // Wait for recovery timeout
    std::this_thread::sleep_for(20ms);
    
    // Next request should be allowed (half-open)
    REQUIRE(breaker.allow_request());
    REQUIRE(breaker.state() == CircuitState::HalfOpen);
}

TEST_CASE("CircuitBreaker closes on success in half-open", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 10ms;
    config.success_threshold = 1;
    CircuitBreaker breaker(config);
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    
    // Wait and transition to half-open
    std::this_thread::sleep_for(20ms);
    breaker.allow_request();
    REQUIRE(breaker.state() == CircuitState::HalfOpen);
    
    // Success closes the circuit
    breaker.record_success();
    REQUIRE(breaker.is_closed());
}

TEST_CASE("CircuitBreaker reopens on failure in half-open", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 10ms;
    CircuitBreaker breaker(config);
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    
    // Wait and transition to half-open
    std::this_thread::sleep_for(20ms);
    breaker.allow_request();
    REQUIRE(breaker.state() == CircuitState::HalfOpen);
    
    // Failure reopens the circuit
    breaker.record_failure();
    REQUIRE(breaker.is_open());
}

TEST_CASE("CircuitBreaker HalfOpen only allows one test request", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 10ms;
    CircuitBreaker breaker(config);
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    
    // Wait and transition to half-open
    std::this_thread::sleep_for(20ms);
    REQUIRE(breaker.allow_request());  // First test request allowed
    REQUIRE(breaker.state() == CircuitState::HalfOpen);
    
    // Second request should be rejected while test is in progress
    REQUIRE_FALSE(breaker.allow_request());
    REQUIRE_FALSE(breaker.allow_request());
    
    auto stats = breaker.stats();
    REQUIRE(stats.rejected_requests == 2);
    
    // After success, next request is allowed
    breaker.record_success();
    REQUIRE(breaker.allow_request());
}

TEST_CASE("CircuitBreaker requires multiple successes to close", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 10ms;
    config.success_threshold = 3;
    CircuitBreaker breaker(config);
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    
    // Wait and transition to half-open
    std::this_thread::sleep_for(20ms);
    REQUIRE(breaker.allow_request());  // First test request
    REQUIRE(breaker.state() == CircuitState::HalfOpen);
    
    // First success - still half-open, allows next request
    breaker.record_success();
    REQUIRE(breaker.state() == CircuitState::HalfOpen);
    
    REQUIRE(breaker.allow_request());  // Second test request
    breaker.record_success();
    REQUIRE(breaker.state() == CircuitState::HalfOpen);
    
    // Third success - closes
    REQUIRE(breaker.allow_request());  // Third test request
    breaker.record_success();
    REQUIRE(breaker.is_closed());
}

// ═══════════════════════════════════════════════════════════════════════════
// Statistics Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreaker tracks statistics", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 5;
    CircuitBreaker breaker(config);
    
    // Make some successful requests
    for (int i = 0; i < 5; ++i) {
        breaker.allow_request();
        breaker.record_success();
    }
    
    // Make 5 consecutive failures to trip the breaker
    for (int i = 0; i < 5; ++i) {
        breaker.allow_request();
        breaker.record_failure();
    }
    
    auto stats = breaker.stats();
    REQUIRE(stats.total_requests == 10);
    REQUIRE(stats.successful_requests == 5);
    REQUIRE(stats.failed_requests == 5);
    REQUIRE(stats.current_state == CircuitState::Open);  // 5 consecutive failures
}

TEST_CASE("CircuitBreaker tracks state transitions", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 10ms;
    CircuitBreaker breaker(config);
    
    // Closed -> Open
    breaker.allow_request();
    breaker.record_failure();
    
    // Wait and Open -> HalfOpen
    std::this_thread::sleep_for(20ms);
    breaker.allow_request();
    
    // HalfOpen -> Closed
    breaker.record_success();
    
    auto stats = breaker.stats();
    REQUIRE(stats.state_transitions == 3);  // Closed->Open, Open->HalfOpen, HalfOpen->Closed
}

// ═══════════════════════════════════════════════════════════════════════════
// Manual Control Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreaker can be manually opened", "[resilience][circuit_breaker]") {
    CircuitBreaker breaker;
    
    REQUIRE(breaker.is_closed());
    
    breaker.force_open();
    
    REQUIRE(breaker.is_open());
    REQUIRE_FALSE(breaker.allow_request());
}

TEST_CASE("CircuitBreaker can be manually closed", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 1h;
    CircuitBreaker breaker(config);
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_open());
    
    // Force close
    breaker.force_close();
    
    REQUIRE(breaker.is_closed());
    REQUIRE(breaker.allow_request());
}

TEST_CASE("CircuitBreaker reset clears all state", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    CircuitBreaker breaker(config);
    
    // Make some requests and trip breaker
    breaker.allow_request();
    breaker.record_failure();
    REQUIRE(breaker.is_open());
    
    // Reset
    breaker.reset();
    
    REQUIRE(breaker.is_closed());
    auto stats = breaker.stats();
    REQUIRE(stats.total_requests == 0);
    REQUIRE(stats.failed_requests == 0);
    REQUIRE(stats.state_transitions == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Callback Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreaker fires state change callbacks", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    config.recovery_timeout = 10ms;
    CircuitBreaker breaker(config);
    
    std::vector<std::pair<CircuitState, CircuitState>> transitions;
    breaker.on_state_change([&](CircuitState old_state, CircuitState new_state) {
        transitions.emplace_back(old_state, new_state);
    });
    
    // Trip the breaker
    breaker.allow_request();
    breaker.record_failure();
    
    REQUIRE(transitions.size() == 1);
    REQUIRE(transitions[0].first == CircuitState::Closed);
    REQUIRE(transitions[0].second == CircuitState::Open);
    
    // Wait and transition to half-open
    std::this_thread::sleep_for(20ms);
    breaker.allow_request();
    
    REQUIRE(transitions.size() == 2);
    REQUIRE(transitions[1].first == CircuitState::Open);
    REQUIRE(transitions[1].second == CircuitState::HalfOpen);
}

// ═══════════════════════════════════════════════════════════════════════════
// Guard Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitBreakerGuard records failure on exception", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    CircuitBreaker breaker(config);
    
    try {
        CircuitBreakerGuard guard(breaker);
        throw std::runtime_error("test");
    } catch (...) {
        // Expected
    }
    
    REQUIRE(breaker.is_open());
}

TEST_CASE("CircuitBreakerGuard records success when marked", "[resilience][circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 1;
    CircuitBreaker breaker(config);
    
    {
        CircuitBreakerGuard guard(breaker);
        guard.mark_success();
    }
    
    REQUIRE(breaker.is_closed());
    auto stats = breaker.stats();
    REQUIRE(stats.successful_requests == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// to_string Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CircuitState to_string returns correct values", "[resilience][circuit_breaker]") {
    REQUIRE(to_string(CircuitState::Closed) == "Closed");
    REQUIRE(to_string(CircuitState::Open) == "Open");
    REQUIRE(to_string(CircuitState::HalfOpen) == "HalfOpen");
}

