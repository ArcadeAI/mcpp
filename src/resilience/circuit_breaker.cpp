#include "mcpp/resilience/circuit_breaker.hpp"

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

CircuitBreaker::CircuitBreaker(CircuitBreakerConfig config)
    : config_(std::move(config))
{}

// ─────────────────────────────────────────────────────────────────────────────
// Core Operations
// ─────────────────────────────────────────────────────────────────────────────

bool CircuitBreaker::allow_request() {
    total_requests_.fetch_add(1, std::memory_order_relaxed);
    
    std::vector<StateChangeCallback> callbacks_to_fire;
    CircuitState old_state, new_state;
    bool should_fire_callbacks = false;
    bool result = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        switch (state_) {
            case CircuitState::Closed:
                result = true;
                break;
                
            case CircuitState::Open:
                // Check if we should try to recover
                if (should_attempt_reset()) {
                    old_state = state_;
                    new_state = CircuitState::HalfOpen;
                    state_ = new_state;
                    state_transitions_.fetch_add(1, std::memory_order_relaxed);
                    consecutive_successes_ = 0;
                    half_open_test_in_progress_ = true;
                    callbacks_to_fire = state_change_callbacks_;
                    should_fire_callbacks = true;
                    result = true;
                } else {
                    rejected_requests_.fetch_add(1, std::memory_order_relaxed);
                    result = false;
                }
                break;
                
            case CircuitState::HalfOpen:
                // Only allow one test request at a time in half-open state
                if (half_open_test_in_progress_) {
                    rejected_requests_.fetch_add(1, std::memory_order_relaxed);
                    result = false;
                } else {
                    half_open_test_in_progress_ = true;
                    result = true;
                }
                break;
        }
    }
    
    // Fire callbacks outside the lock to avoid deadlock
    if (should_fire_callbacks) {
        for (const auto& callback : callbacks_to_fire) {
            callback(old_state, new_state);
        }
    }
    
    return result;
}

void CircuitBreaker::record_success() {
    successful_requests_.fetch_add(1, std::memory_order_relaxed);
    
    std::vector<StateChangeCallback> callbacks_to_fire;
    CircuitState old_state, new_state;
    bool should_fire_callbacks = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        consecutive_failures_ = 0;
        half_open_test_in_progress_ = false;
        
        switch (state_) {
            case CircuitState::Closed:
                // Normal operation, nothing to do
                break;
                
            case CircuitState::HalfOpen:
                consecutive_successes_++;
                if (consecutive_successes_ >= config_.success_threshold) {
                    old_state = state_;
                    new_state = CircuitState::Closed;
                    state_ = new_state;
                    state_transitions_.fetch_add(1, std::memory_order_relaxed);
                    callbacks_to_fire = state_change_callbacks_;
                    should_fire_callbacks = true;
                }
                break;
                
            case CircuitState::Open:
                // Shouldn't happen (requests are rejected), but handle gracefully
                break;
        }
    }
    
    if (should_fire_callbacks) {
        for (const auto& callback : callbacks_to_fire) {
            callback(old_state, new_state);
        }
    }
}

void CircuitBreaker::record_failure() {
    failed_requests_.fetch_add(1, std::memory_order_relaxed);
    
    std::vector<StateChangeCallback> callbacks_to_fire;
    CircuitState old_state, new_state;
    bool should_fire_callbacks = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        consecutive_successes_ = 0;
        consecutive_failures_++;
        last_failure_time_ = std::chrono::steady_clock::now();
        half_open_test_in_progress_ = false;
        
        switch (state_) {
            case CircuitState::Closed:
                if (consecutive_failures_ >= config_.failure_threshold) {
                    old_state = state_;
                    new_state = CircuitState::Open;
                    state_ = new_state;
                    state_transitions_.fetch_add(1, std::memory_order_relaxed);
                    callbacks_to_fire = state_change_callbacks_;
                    should_fire_callbacks = true;
                }
                break;
                
            case CircuitState::HalfOpen:
                // Test request failed, go back to open
                old_state = state_;
                new_state = CircuitState::Open;
                state_ = new_state;
                state_transitions_.fetch_add(1, std::memory_order_relaxed);
                callbacks_to_fire = state_change_callbacks_;
                should_fire_callbacks = true;
                break;
                
            case CircuitState::Open:
                // Already open, update failure time
                break;
        }
    }
    
    if (should_fire_callbacks) {
        for (const auto& callback : callbacks_to_fire) {
            callback(old_state, new_state);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// State Queries
// ─────────────────────────────────────────────────────────────────────────────

CircuitState CircuitBreaker::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool CircuitBreaker::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == CircuitState::Open;
}

bool CircuitBreaker::is_closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == CircuitState::Closed;
}

CircuitBreakerStats CircuitBreaker::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return CircuitBreakerStats{
        .total_requests = total_requests_.load(std::memory_order_relaxed),
        .successful_requests = successful_requests_.load(std::memory_order_relaxed),
        .failed_requests = failed_requests_.load(std::memory_order_relaxed),
        .rejected_requests = rejected_requests_.load(std::memory_order_relaxed),
        .state_transitions = state_transitions_.load(std::memory_order_relaxed),
        .current_state = state_
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Manual Control
// ─────────────────────────────────────────────────────────────────────────────

void CircuitBreaker::force_open() {
    std::vector<StateChangeCallback> callbacks_to_fire;
    CircuitState old_state, new_state;
    bool should_fire_callbacks = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != CircuitState::Open) {
            old_state = state_;
            new_state = CircuitState::Open;
            state_ = new_state;
            last_failure_time_ = std::chrono::steady_clock::now();
            half_open_test_in_progress_ = false;
            state_transitions_.fetch_add(1, std::memory_order_relaxed);
            callbacks_to_fire = state_change_callbacks_;
            should_fire_callbacks = true;
        }
    }
    
    if (should_fire_callbacks) {
        for (const auto& callback : callbacks_to_fire) {
            callback(old_state, new_state);
        }
    }
}

void CircuitBreaker::force_close() {
    std::vector<StateChangeCallback> callbacks_to_fire;
    CircuitState old_state, new_state;
    bool should_fire_callbacks = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != CircuitState::Closed) {
            old_state = state_;
            new_state = CircuitState::Closed;
            state_ = new_state;
            consecutive_failures_ = 0;
            consecutive_successes_ = 0;
            half_open_test_in_progress_ = false;
            state_transitions_.fetch_add(1, std::memory_order_relaxed);
            callbacks_to_fire = state_change_callbacks_;
            should_fire_callbacks = true;
        }
    }
    
    if (should_fire_callbacks) {
        for (const auto& callback : callbacks_to_fire) {
            callback(old_state, new_state);
        }
    }
}

void CircuitBreaker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    state_ = CircuitState::Closed;
    consecutive_failures_ = 0;
    consecutive_successes_ = 0;
    half_open_test_in_progress_ = false;
    
    total_requests_.store(0, std::memory_order_relaxed);
    successful_requests_.store(0, std::memory_order_relaxed);
    failed_requests_.store(0, std::memory_order_relaxed);
    rejected_requests_.store(0, std::memory_order_relaxed);
    state_transitions_.store(0, std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void CircuitBreaker::on_state_change(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_change_callbacks_.push_back(std::move(callback));
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool CircuitBreaker::should_attempt_reset() const {
    // Caller must hold mutex
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_failure_time_
    );
    return elapsed >= config_.recovery_timeout;
}

}  // namespace mcpp
