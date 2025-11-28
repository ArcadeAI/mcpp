#include "mcpp/transport/http_client.hpp"

#include <cpr/cpr.h>

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// CprHttpClient Implementation
// ─────────────────────────────────────────────────────────────────────────────
// Uses cpr (C++ Requests) library - a modern wrapper around libcurl.
// Provides both sync and async operations.
//
// Why cpr?
// - Clean C++ API (inspired by Python requests)
// - Built-in async via std::future
// - Handles SSL, cookies, redirects automatically
// - Battle-tested (libcurl underneath)

class CprHttpClient : public IHttpClient {
public:
    CprHttpClient() = default;
    ~CprHttpClient() override = default;

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────

    void set_base_url(const std::string& url) override {
        base_url_ = url;
    }

    void set_default_headers(const HeaderMap& headers) override {
        default_headers_ = headers;
    }

    void set_connect_timeout(std::chrono::milliseconds timeout) override {
        connect_timeout_ = timeout;
    }

    void set_read_timeout(std::chrono::milliseconds timeout) override {
        read_timeout_ = timeout;
    }

    void set_verify_ssl(bool verify) override {
        verify_ssl_ = verify;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Synchronous Operations
    // ─────────────────────────────────────────────────────────────────────────

    HttpClientResult<HttpClientResponse> get(
        const std::string& path,
        const HeaderMap& headers
    ) override {
        const bool is_cancelled = cancelled_.load();
        if (is_cancelled) {
            return tl::unexpected(HttpClientError::cancelled());
        }

        auto response = cpr::Get(
            cpr::Url{build_url(path)},
            build_headers(headers),
            cpr::ConnectTimeout{connect_timeout_},
            cpr::Timeout{read_timeout_},
            cpr::VerifySsl{verify_ssl_}
        );
        return convert_response(response);
    }

    HttpClientResult<HttpClientResponse> post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers
    ) override {
        const bool is_cancelled = cancelled_.load();
        if (is_cancelled) {
            return tl::unexpected(HttpClientError::cancelled());
        }

        auto request_headers = build_headers(headers);
        request_headers["Content-Type"] = content_type;

        auto response = cpr::Post(
            cpr::Url{build_url(path)},
            request_headers,
            cpr::Body{body},
            cpr::ConnectTimeout{connect_timeout_},
            cpr::Timeout{read_timeout_},
            cpr::VerifySsl{verify_ssl_}
        );
        return convert_response(response);
    }

    HttpClientResult<HttpClientResponse> del(
        const std::string& path,
        const HeaderMap& headers
    ) override {
        const bool is_cancelled = cancelled_.load();
        if (is_cancelled) {
            return tl::unexpected(HttpClientError::cancelled());
        }

        auto response = cpr::Delete(
            cpr::Url{build_url(path)},
            build_headers(headers),
            cpr::ConnectTimeout{connect_timeout_},
            cpr::Timeout{read_timeout_},
            cpr::VerifySsl{verify_ssl_}
        );
        return convert_response(response);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Asynchronous Operations
    // ─────────────────────────────────────────────────────────────────────────

    std::future<HttpClientResult<HttpClientResponse>> async_get(
        const std::string& path,
        const HeaderMap& headers
    ) override {
        // cpr provides async via cpr::GetAsync, but it returns cpr::AsyncResponse
        // which is a bit different. We wrap in std::async for consistency.
        return std::async(std::launch::async, [this, path, headers]() {
            return this->get(path, headers);
        });
    }

    std::future<HttpClientResult<HttpClientResponse>> async_post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers
    ) override {
        return std::async(std::launch::async, [this, path, body, content_type, headers]() {
            return this->post(path, body, content_type, headers);
        });
    }

    std::future<HttpClientResult<HttpClientResponse>> async_del(
        const std::string& path,
        const HeaderMap& headers
    ) override {
        return std::async(std::launch::async, [this, path, headers]() {
            return this->del(path, headers);
        });
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    void cancel() override {
        cancelled_.store(true);
        // Note: cpr doesn't have a clean way to cancel in-flight requests.
        // The cancelled_ flag prevents new requests from starting.
        // In-flight requests will complete naturally.
    }
    
    void reset() override {
        // Reset the cancelled flag to allow new requests after stop/start cycle
        cancelled_.store(false);
    }

private:
    // Build URL and headers for request
    // Validates path to prevent path traversal attacks
    std::string build_url(const std::string& path) {
        // Reject paths with traversal patterns
        if (path.find("..") != std::string::npos) {
            throw std::invalid_argument("Path traversal detected in URL path");
        }
        
        // Reject paths with encoded traversal (%2e%2e or %2E%2E)
        std::string path_lower = path;
        std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        if (path_lower.find("%2e%2e") != std::string::npos) {
            throw std::invalid_argument("Encoded path traversal detected in URL path");
        }
        
        // Ensure path starts with / if not empty
        if (!path.empty() && path[0] != '/') {
            return base_url_ + "/" + path;
        }
        return base_url_ + path;
    }

    cpr::Header build_headers(const HeaderMap& extra_headers) {
        cpr::Header cpr_headers;
        for (const auto& [name, value] : default_headers_) {
            cpr_headers[name] = value;
        }
        for (const auto& [name, value] : extra_headers) {
            cpr_headers[name] = value;
        }
        return cpr_headers;
    }

    // Convert cpr::Response to our HttpClientResponse
    HttpClientResult<HttpClientResponse> convert_response(const cpr::Response& response) {
        // Check for errors
        const bool has_error = (response.error.code != cpr::ErrorCode::OK);
        if (has_error) {
            return tl::unexpected(map_error(response.error));
        }

        // Build our response
        HttpClientResponse result;
        result.status_code = static_cast<int>(response.status_code);
        result.body = response.text;

        // Convert headers
        for (const auto& [name, value] : response.header) {
            result.headers[name] = value;
        }

        return result;
    }

    // Map cpr errors to our error type
    HttpClientError map_error(const cpr::Error& error) {
        // cpr::ErrorCode values (simplified mapping)
        // Check if the message contains SSL-related keywords
        const std::string& msg = error.message;
        const bool is_ssl_error = 
            (msg.find("SSL") != std::string::npos) ||
            (msg.find("ssl") != std::string::npos) ||
            (msg.find("certificate") != std::string::npos) ||
            (msg.find("TLS") != std::string::npos);

        if (is_ssl_error) {
            return HttpClientError::ssl_error(msg);
        }

        switch (error.code) {
            case cpr::ErrorCode::OK:
                return HttpClientError::unknown("No error");

            case cpr::ErrorCode::OPERATION_TIMEDOUT:
                return HttpClientError::timeout(msg);

            case cpr::ErrorCode::SSL_CONNECT_ERROR:
                return HttpClientError::ssl_error(msg);

            default:
                // Most other errors are connection-related
                return HttpClientError::connection_failed(msg);
        }
    }

    // Configuration
    std::string base_url_;
    HeaderMap default_headers_;
    std::chrono::milliseconds connect_timeout_{10000};
    std::chrono::milliseconds read_timeout_{30000};
    bool verify_ssl_{true};

    // State
    std::atomic<bool> cancelled_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory Function
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<IHttpClient> make_http_client() {
    return std::make_unique<CprHttpClient>();
}

}  // namespace mcpp

