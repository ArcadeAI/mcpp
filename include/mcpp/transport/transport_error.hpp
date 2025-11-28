#ifndef MCPP_TRANSPORT_TRANSPORT_ERROR_HPP
#define MCPP_TRANSPORT_TRANSPORT_ERROR_HPP

#include "mcpp/transport/http_client.hpp"

#include <optional>
#include <string>

#include <tl/expected.hpp>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Transport Error Types
// ─────────────────────────────────────────────────────────────────────────────

struct HttpTransportError {
    enum class Code {
        ConnectionFailed,    // Could not connect to server
        Timeout,             // Request timed out
        SslError,            // TLS/SSL handshake or verification failed
        InvalidResponse,     // Server returned malformed response
        HttpError,           // Server returned error status code
        SessionExpired,      // Mcp-Session-Id no longer valid (404)
        Closed,              // Transport was closed
        ParseError           // Failed to parse JSON response
    };

    Code code;
    std::string message;
    std::optional<int> http_status;  // HTTP status code if applicable

    static HttpTransportError connection_failed(const std::string& msg) {
        return {Code::ConnectionFailed, msg, std::nullopt};
    }

    static HttpTransportError timeout(const std::string& msg) {
        return {Code::Timeout, msg, std::nullopt};
    }

    static HttpTransportError ssl_error(const std::string& msg) {
        return {Code::SslError, msg, std::nullopt};
    }

    static HttpTransportError http_error(int status, const std::string& msg) {
        return {Code::HttpError, msg, status};
    }

    static HttpTransportError session_expired() {
        return {Code::SessionExpired, "Session expired (404)", 404};
    }

    static HttpTransportError closed() {
        return {Code::Closed, "Transport is closed", std::nullopt};
    }

    static HttpTransportError parse_error(const std::string& msg) {
        return {Code::ParseError, msg, std::nullopt};
    }

    // Convert from HttpClientError
    static HttpTransportError from_client_error(const HttpClientError& err) {
        switch (err.code) {
            case HttpClientError::Code::ConnectionFailed:
                return connection_failed(err.message);
            case HttpClientError::Code::Timeout:
                return timeout(err.message);
            case HttpClientError::Code::SslError:
                return ssl_error(err.message);
            case HttpClientError::Code::Cancelled:
                return closed();
            default:
                return connection_failed(err.message);
        }
    }
};

template <typename T>
using HttpResult = tl::expected<T, HttpTransportError>;

}  // namespace mcpp

#endif  // MCPP_TRANSPORT_TRANSPORT_ERROR_HPP

