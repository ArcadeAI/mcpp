#ifndef MCPP_TRANSPORT_BACKOFF_POLICY_HPP
#define MCPP_TRANSPORT_BACKOFF_POLICY_HPP

#include <chrono>
#include <cmath>
#include <cstddef>
#include <random>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// IBackoffPolicy — Strategy Pattern Interface
// ─────────────────────────────────────────────────────────────────────────────
// Defines how to calculate delays between retry attempts.
//
// Why an interface?
// - Different servers may need different retry strategies
// - Testing: inject a "no delay" policy for fast tests
// - Future: adaptive policies based on server hints (Retry-After header)
//
// Usage:
//   auto policy = std::make_shared<ExponentialBackoff>();
//   for (size_t attempt = 0; attempt < max_retries; ++attempt) {
//       auto delay = policy->next_delay(attempt);
//       std::this_thread::sleep_for(delay);
//       // try request...
//   }

struct IBackoffPolicy {
    virtual ~IBackoffPolicy() = default;

    // Calculate delay before the next retry attempt.
    // attempt: 0-indexed retry number (0 = first retry after initial failure)
    virtual std::chrono::milliseconds next_delay(std::size_t attempt) = 0;

    // Reset internal state (e.g., after successful request).
    virtual void reset() = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ExponentialBackoff — Concrete Strategy
// ─────────────────────────────────────────────────────────────────────────────
// Implements exponential backoff with optional jitter.
//
// Formula: delay = min(base * (multiplier ^ attempt) + jitter, max)
//
// Example with base=100ms, multiplier=2.0, max=5s:
//   Attempt 0: 100ms * 2^0 = 100ms  + jitter
//   Attempt 1: 100ms * 2^1 = 200ms  + jitter
//   Attempt 2: 100ms * 2^2 = 400ms  + jitter
//   Attempt 3: 100ms * 2^3 = 800ms  + jitter
//   Attempt 4: 100ms * 2^4 = 1600ms + jitter
//   Attempt 5+: capped at 5000ms    + jitter
//
// Why jitter?
// - Prevents "thundering herd" when many clients retry simultaneously
// - Spreads load on recovering servers
// - Makes retry timing less predictable (good for avoiding rate limits)

class ExponentialBackoff : public IBackoffPolicy {
public:
    // Default: 100ms base, 2x multiplier, 30s max, ±25% jitter
    ExponentialBackoff()
        : ExponentialBackoff(
              std::chrono::milliseconds{100},
              2.0,
              std::chrono::milliseconds{30'000},
              0.25
          ) {}

    // Custom configuration
    ExponentialBackoff(
        std::chrono::milliseconds base,
        double multiplier,
        std::chrono::milliseconds max,
        double jitter_factor  // 0.0 = no jitter, 0.25 = ±25%
    )
        : base_(base)
        , multiplier_(multiplier)
        , max_(max)
        , jitter_factor_(jitter_factor)
        , rng_(std::random_device{}())
    {}

    std::chrono::milliseconds next_delay(std::size_t attempt) override {
        // Calculate base delay with exponential growth
        const double exponent = static_cast<double>(attempt);
        const double base_ms = static_cast<double>(base_.count());
        const double delay_ms = base_ms * std::pow(multiplier_, exponent);

        // Cap at maximum
        const double max_ms = static_cast<double>(max_.count());
        const double capped_ms = std::min(delay_ms, max_ms);

        // Add jitter
        const double jittered_ms = add_jitter(capped_ms);

        // Convert back to milliseconds (ensure non-negative)
        const auto result_ms = static_cast<std::int64_t>(std::max(0.0, jittered_ms));
        return std::chrono::milliseconds{result_ms};
    }

    void reset() override {
        // No internal state to reset for this implementation.
        // Some advanced policies might track consecutive failures, etc.
    }

private:
    double add_jitter(double base_value) {
        const bool has_jitter = (jitter_factor_ > 0.0);
        if (has_jitter == false) {
            return base_value;
        }

        // Generate random factor in range [1 - jitter, 1 + jitter]
        std::uniform_real_distribution<double> dist(
            1.0 - jitter_factor_,
            1.0 + jitter_factor_
        );
        const double jitter_multiplier = dist(rng_);
        return base_value * jitter_multiplier;
    }

    std::chrono::milliseconds base_;
    double multiplier_;
    std::chrono::milliseconds max_;
    double jitter_factor_;
    std::mt19937 rng_;  // Mersenne Twister random generator
};

// ─────────────────────────────────────────────────────────────────────────────
// NoBackoff — Testing Helper
// ─────────────────────────────────────────────────────────────────────────────
// Returns zero delay — useful for fast unit tests.

class NoBackoff : public IBackoffPolicy {
public:
    std::chrono::milliseconds next_delay(std::size_t /*attempt*/) override {
        return std::chrono::milliseconds{0};
    }

    void reset() override {}
};

// ─────────────────────────────────────────────────────────────────────────────
// ConstantBackoff — Simple Fixed Delay
// ─────────────────────────────────────────────────────────────────────────────
// Always returns the same delay — useful for testing or simple scenarios.

class ConstantBackoff : public IBackoffPolicy {
public:
    explicit ConstantBackoff(std::chrono::milliseconds delay)
        : delay_(delay) {}

    std::chrono::milliseconds next_delay(std::size_t /*attempt*/) override {
        return delay_;
    }

    void reset() override {}

private:
    std::chrono::milliseconds delay_;
};

}  // namespace mcpp

#endif  // MCPP_TRANSPORT_BACKOFF_POLICY_HPP


