#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// Circuit Breaker
// ═══════════════════════════════════════════════════════════════════════════
// Implements the circuit breaker pattern to prevent cascading failures.
//
// State Machine:
//
//   ┌─────────┐  failure_threshold   ┌────────┐
//   │ CLOSED  │ ─────────────────────▶│  OPEN  │
//   └────┬────┘   consecutive         └────┬───┘
//        │        failures                 │
//        │                                 │ recovery_timeout
//        │                                 ▼
//        │                           ┌──────────┐
//        │                           │HALF_OPEN │
//        │                           └────┬─────┘
//        │                                │
//        │◀───────────────────────────────┤ success
//        │         reset to closed        │
//        │                                │ failure
//        │                                ▼
//        │                           ┌────────┐
//        └───────────────────────────│  OPEN  │
//                                    └────────┘
//
// Usage:
//   CircuitBreaker breaker(CircuitBreakerConfig{
//       .failure_threshold = 5,
//       .recovery_timeout = std::chrono::seconds(30)
//   });
//
//   if (breaker.allow_request()) {
//       try {
//           auto result = make_request();
//           breaker.record_success();
//           return result;
//       } catch (...) {
//           breaker.record_failure();
//           throw;
//       }
//   } else {
//       throw CircuitOpenException();
//   }

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Circuit Breaker State
// ─────────────────────────────────────────────────────────────────────────────

enum class CircuitState {
    Closed,    ///< Normal operation, requests pass through
    Open,      ///< Circuit tripped, requests rejected immediately
    HalfOpen   ///< Testing if service recovered
};

/// Convert state to string for logging
[[nodiscard]] constexpr std::string_view to_string(CircuitState state) noexcept {
    switch (state) {
        case CircuitState::Closed:   return "Closed";
        case CircuitState::Open:     return "Open";
        case CircuitState::HalfOpen: return "HalfOpen";
        default:                     return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Circuit Breaker Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct CircuitBreakerConfig {
    /// Number of consecutive failures before opening the circuit
    std::size_t failure_threshold{5};
    
    /// Time to wait before transitioning from Open to HalfOpen
    std::chrono::milliseconds recovery_timeout{30000};  // 30 seconds
    
    /// Number of successful requests in HalfOpen before closing
    std::size_t success_threshold{1};
    
    /// Optional name for logging/metrics
    std::string name{"default"};
};

// ─────────────────────────────────────────────────────────────────────────────
// Circuit Breaker Statistics
// ─────────────────────────────────────────────────────────────────────────────

struct CircuitBreakerStats {
    std::size_t total_requests{0};
    std::size_t successful_requests{0};
    std::size_t failed_requests{0};
    std::size_t rejected_requests{0};  ///< Requests rejected due to open circuit
    std::size_t state_transitions{0};
    CircuitState current_state{CircuitState::Closed};
};

// ─────────────────────────────────────────────────────────────────────────────
// Circuit Breaker
// ─────────────────────────────────────────────────────────────────────────────

class CircuitBreaker {
public:
    /// Callback for state changes
    using StateChangeCallback = std::function<void(CircuitState old_state, CircuitState new_state)>;
    
    /// Create with default configuration
    CircuitBreaker() = default;
    
    /// Create with custom configuration
    explicit CircuitBreaker(CircuitBreakerConfig config);
    
    // Non-copyable, non-movable (due to mutex)
    CircuitBreaker(const CircuitBreaker&) = delete;
    CircuitBreaker& operator=(const CircuitBreaker&) = delete;
    CircuitBreaker(CircuitBreaker&&) = delete;
    CircuitBreaker& operator=(CircuitBreaker&&) = delete;
    
    ~CircuitBreaker() = default;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Core Operations
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Check if a request should be allowed
    /// Returns true if request can proceed, false if circuit is open
    [[nodiscard]] bool allow_request();
    
    /// Record a successful request
    void record_success();
    
    /// Record a failed request
    void record_failure();
    
    // ─────────────────────────────────────────────────────────────────────────
    // State Queries
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Get current circuit state
    [[nodiscard]] CircuitState state() const;
    
    /// Check if circuit is open (rejecting requests)
    [[nodiscard]] bool is_open() const;
    
    /// Check if circuit is closed (normal operation)
    [[nodiscard]] bool is_closed() const;
    
    /// Get statistics
    [[nodiscard]] CircuitBreakerStats stats() const;
    
    /// Get configuration
    [[nodiscard]] const CircuitBreakerConfig& config() const noexcept { return config_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Manual Control
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Force the circuit to open (for testing or manual intervention)
    void force_open();
    
    /// Force the circuit to close (for testing or manual intervention)
    void force_close();
    
    /// Reset all statistics and state
    void reset();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Register callback for state changes
    void on_state_change(StateChangeCallback callback);

private:
    bool should_attempt_reset() const;
    
    CircuitBreakerConfig config_;
    
    mutable std::mutex mutex_;
    CircuitState state_{CircuitState::Closed};
    std::size_t consecutive_failures_{0};
    std::size_t consecutive_successes_{0};
    std::chrono::steady_clock::time_point last_failure_time_{std::chrono::steady_clock::now()};
    bool half_open_test_in_progress_{false};  // Track if a test request is in flight
    
    // Statistics
    std::atomic<std::size_t> total_requests_{0};
    std::atomic<std::size_t> successful_requests_{0};
    std::atomic<std::size_t> failed_requests_{0};
    std::atomic<std::size_t> rejected_requests_{0};
    std::atomic<std::size_t> state_transitions_{0};
    
    // Callbacks
    std::vector<StateChangeCallback> state_change_callbacks_;
};

// ─────────────────────────────────────────────────────────────────────────────
// RAII Guard for Circuit Breaker
// ─────────────────────────────────────────────────────────────────────────────
// Automatically records success/failure based on scope exit

class CircuitBreakerGuard {
public:
    explicit CircuitBreakerGuard(CircuitBreaker& breaker)
        : breaker_(breaker)
        , success_(false)
    {}
    
    ~CircuitBreakerGuard() {
        if (success_) {
            breaker_.record_success();
        } else {
            breaker_.record_failure();
        }
    }
    
    // Non-copyable, non-movable
    CircuitBreakerGuard(const CircuitBreakerGuard&) = delete;
    CircuitBreakerGuard& operator=(const CircuitBreakerGuard&) = delete;
    CircuitBreakerGuard(CircuitBreakerGuard&&) = delete;
    CircuitBreakerGuard& operator=(CircuitBreakerGuard&&) = delete;
    
    /// Mark the operation as successful (call before scope exit)
    void mark_success() noexcept { success_ = true; }
    
private:
    CircuitBreaker& breaker_;
    bool success_;
};

}  // namespace mcpp


