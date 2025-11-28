#pragma once

#include "mcpp/transport/http_types.hpp"

#include <tl/expected.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <string>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// HTTP Client Error
// ─────────────────────────────────────────────────────────────────────────────

struct HttpClientError {
    enum class Code {
        ConnectionFailed,
        Timeout,
        SslError,
        Cancelled,
        Unknown
    };

    Code code;
    std::string message;

    static HttpClientError connection_failed(const std::string& msg) {
        return {Code::ConnectionFailed, msg};
    }
    static HttpClientError timeout(const std::string& msg) {
        return {Code::Timeout, msg};
    }
    static HttpClientError ssl_error(const std::string& msg) {
        return {Code::SslError, msg};
    }
    static HttpClientError cancelled() {
        return {Code::Cancelled, "Request cancelled"};
    }
    static HttpClientError unknown(const std::string& msg) {
        return {Code::Unknown, msg};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// HTTP Client Response
// ─────────────────────────────────────────────────────────────────────────────

struct HttpClientResponse {
    int status_code{0};
    HeaderMap headers;
    std::string body;

    [[nodiscard]] bool is_success() const {
        return (status_code >= 200) && (status_code < 300);
    }

    [[nodiscard]] bool is_sse() const {
        const auto content_type = get_header(headers, "Content-Type");
        const bool found = content_type.has_value();
        if (found == false)
        {
            return false;
        }
        return content_type->find("text/event-stream") != std::string::npos;
    }

    [[nodiscard]] bool is_json() const {
        const auto content_type = get_header(headers, "Content-Type");
        const bool found = content_type.has_value();
        if (found == false) return false;
        return content_type->find("application/json") != std::string::npos;
    }
};

template <typename T>
using HttpClientResult = tl::expected<T, HttpClientError>;

// ─────────────────────────────────────────────────────────────────────────────
// IHttpClient Interface
// ─────────────────────────────────────────────────────────────────────────────
// Abstract interface for HTTP clients. This allows us to:
// - Swap implementations (cpr, Beast, libcurl, etc.)
// - Mock for testing
// - Add decorators (logging, metrics, retry)
//
// Design: We use std::future for async operations. This is a simple model
// that works with any backend. More advanced backends (ASIO) could provide
// coroutine-based alternatives.

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────

    // Set base URL (e.g., "https://api.example.com")
    virtual void set_base_url(const std::string& url) = 0;

    // Set default headers sent with every request
    virtual void set_default_headers(const HeaderMap& headers) = 0;

    // Set connection timeout
    virtual void set_connect_timeout(std::chrono::milliseconds timeout) = 0;

    // Set read timeout (time to wait for response data)
    virtual void set_read_timeout(std::chrono::milliseconds timeout) = 0;

    // Enable/disable SSL certificate verification
    virtual void set_verify_ssl(bool verify) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Synchronous Operations
    // ─────────────────────────────────────────────────────────────────────────

    // Perform HTTP GET
    [[nodiscard]] virtual HttpClientResult<HttpClientResponse> get(
        const std::string& path,
        const HeaderMap& headers = {}
    ) = 0;

    // Perform HTTP POST
    [[nodiscard]] virtual HttpClientResult<HttpClientResponse> post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers = {}
    ) = 0;

    // Perform HTTP DELETE
    [[nodiscard]] virtual HttpClientResult<HttpClientResponse> del(
        const std::string& path,
        const HeaderMap& headers = {}
    ) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Asynchronous Operations
    // ─────────────────────────────────────────────────────────────────────────
    // These return std::future for easy async/await style usage.

    [[nodiscard]] virtual std::future<HttpClientResult<HttpClientResponse>> async_get(
        const std::string& path,
        const HeaderMap& headers = {}
    ) = 0;

    [[nodiscard]] virtual std::future<HttpClientResult<HttpClientResponse>> async_post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers = {}
    ) = 0;

    [[nodiscard]] virtual std::future<HttpClientResult<HttpClientResponse>> async_del(
        const std::string& path,
        const HeaderMap& headers = {}
    ) = 0;

    // ─────────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    // Cancel all pending requests
    virtual void cancel() = 0;
    
    // Reset the client for reuse after cancel (allows stop/start cycles)
    virtual void reset() = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory Function
// ─────────────────────────────────────────────────────────────────────────────
// Creates the default HTTP client implementation.
// Currently uses cpr, but can be swapped without changing calling code.

std::unique_ptr<IHttpClient> make_http_client();

}  // namespace mcpp


