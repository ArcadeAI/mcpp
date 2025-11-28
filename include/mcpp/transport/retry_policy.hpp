#ifndef MCPP_TRANSPORT_RETRY_POLICY_HPP
#define MCPP_TRANSPORT_RETRY_POLICY_HPP

#include "mcpp/transport/transport_error.hpp"

#include <cstddef>
#include <set>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// RetryPolicy
// ─────────────────────────────────────────────────────────────────────────────
// Defines when to retry failed HTTP requests.
//
// This complements IBackoffPolicy (which defines *how long* to wait) by
// defining *which* errors should trigger a retry.
//
// Default behavior:
// - Retry on: connection failures, timeouts, 5xx errors, 429
// - Don't retry on: SSL errors, 4xx errors, parse errors, closed transport
//
// Usage:
//   RetryPolicy policy;
//   policy.with_max_attempts(5)
//         .with_retry_on_timeout(true)
//         .with_retryable_status(418);
//
//   if (policy.should_retry(error_code, attempt)) {
//       // retry the request
//   }

class RetryPolicy {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Construction
    // ─────────────────────────────────────────────────────────────────────────

    RetryPolicy()
        : max_attempts_(3)
        , retry_on_connection_error_(true)
        , retry_on_timeout_(true)
        , retry_on_ssl_error_(false)
        , retryable_http_statuses_{429, 500, 502, 503, 504}
    {}

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration (Builder Pattern)
    // ─────────────────────────────────────────────────────────────────────────

    /// Set maximum number of retry attempts (not including initial request).
    RetryPolicy& with_max_attempts(std::size_t attempts) {
        max_attempts_ = attempts;
        return *this;
    }

    /// Enable/disable retry on connection errors.
    RetryPolicy& with_retry_on_connection_error(bool enable) {
        retry_on_connection_error_ = enable;
        return *this;
    }

    /// Enable/disable retry on timeout errors.
    RetryPolicy& with_retry_on_timeout(bool enable) {
        retry_on_timeout_ = enable;
        return *this;
    }

    /// Enable/disable retry on SSL/TLS errors.
    RetryPolicy& with_retry_on_ssl_error(bool enable) {
        retry_on_ssl_error_ = enable;
        return *this;
    }

    /// Add an HTTP status code to the retryable set.
    RetryPolicy& with_retryable_status(int status_code) {
        retryable_http_statuses_.insert(status_code);
        return *this;
    }

    /// Remove an HTTP status code from the retryable set.
    RetryPolicy& without_retryable_status(int status_code) {
        retryable_http_statuses_.erase(status_code);
        return *this;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Query Methods
    // ─────────────────────────────────────────────────────────────────────────

    /// Get maximum retry attempts.
    [[nodiscard]] std::size_t max_attempts() const noexcept {
        return max_attempts_;
    }

    /// Check if a transport error should trigger a retry.
    /// @param code The error code from HttpTransportError
    /// @param attempt Current attempt number (0 = first retry after initial failure)
    [[nodiscard]] bool should_retry(HttpTransportError::Code code, std::size_t attempt) const {
        // Check attempt limit first
        const bool within_limit = (attempt < max_attempts_);
        if (within_limit == false) {
            return false;
        }

        // Check error type
        switch (code) {
            case HttpTransportError::Code::ConnectionFailed:
                return retry_on_connection_error_;

            case HttpTransportError::Code::Timeout:
                return retry_on_timeout_;

            case HttpTransportError::Code::SslError:
                return retry_on_ssl_error_;

            // These are never retryable
            case HttpTransportError::Code::InvalidResponse:
            case HttpTransportError::Code::SessionExpired:  // Handled by session manager
            case HttpTransportError::Code::Closed:
            case HttpTransportError::Code::ParseError:
            case HttpTransportError::Code::HttpError:  // Use should_retry_http_status instead
                return false;
        }

        return false;
    }

    /// Check if an HTTP status code should trigger a retry.
    [[nodiscard]] bool should_retry_http_status(int status_code) const {
        return retryable_http_statuses_.contains(status_code);
    }

private:
    std::size_t max_attempts_;
    bool retry_on_connection_error_;
    bool retry_on_timeout_;
    bool retry_on_ssl_error_;
    std::set<int> retryable_http_statuses_;
};

}  // namespace mcpp

#endif  // MCPP_TRANSPORT_RETRY_POLICY_HPP

