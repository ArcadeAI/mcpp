#ifndef MCPP_TESTS_MOCKS_MOCK_HTTP_CLIENT_HPP
#define MCPP_TESTS_MOCKS_MOCK_HTTP_CLIENT_HPP

#include "mcpp/transport/http_client.hpp"

#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mcpp::testing {

// ─────────────────────────────────────────────────────────────────────────────
// MockHttpClient - Test double for IHttpClient
// ─────────────────────────────────────────────────────────────────────────────
// Allows tests to:
// - Queue canned responses
// - Verify requests were made correctly
// - Simulate errors and timeouts
// - Inspect request history

struct RecordedRequest {
    HttpMethod method;
    std::string path;
    std::string body;
    std::string content_type;
    HeaderMap headers;
};

class MockHttpClient final : public IHttpClient {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Test Setup - Queue Responses
    // ─────────────────────────────────────────────────────────────────────────

    // Queue a successful response (will be returned by next matching request)
    void queue_response(int status_code, const std::string& body, const HeaderMap& headers = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        QueuedResponse resp;
        resp.result = HttpClientResponse{status_code, headers, body};
        response_queue_.push_back(std::move(resp));
    }

    // Queue an SSE response
    void queue_sse_response(const std::string& body) {
        HeaderMap headers;
        headers["Content-Type"] = "text/event-stream";
        queue_response(200, body, headers);
    }

    // Queue a JSON response
    void queue_json_response(int status_code, const std::string& body) {
        HeaderMap headers;
        headers["Content-Type"] = "application/json";
        queue_response(status_code, body, headers);
    }

    // Queue a response with session ID
    void queue_response_with_session(int status_code, const std::string& body, 
                                      const std::string& session_id) {
        HeaderMap headers;
        headers["Content-Type"] = "application/json";
        headers["Mcp-Session-Id"] = session_id;
        queue_response(status_code, body, headers);
    }

    // Queue an error response
    void queue_error(HttpClientError::Code code, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        QueuedResponse resp;
        resp.error = HttpClientError{code, message};
        response_queue_.push_back(std::move(resp));
    }

    // Queue a connection error
    void queue_connection_error(const std::string& message = "Connection refused") {
        queue_error(HttpClientError::Code::ConnectionFailed, message);
    }

    // Queue a timeout error
    void queue_timeout(const std::string& message = "Request timed out") {
        queue_error(HttpClientError::Code::Timeout, message);
    }

    // Queue an SSL error
    void queue_ssl_error(const std::string& message = "SSL handshake failed") {
        queue_error(HttpClientError::Code::SslError, message);
    }

    // Set a response handler for dynamic responses
    using ResponseHandler = std::function<HttpClientResult<HttpClientResponse>(
        HttpMethod method, const std::string& path, const std::string& body
    )>;

    void set_response_handler(ResponseHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        response_handler_ = std::move(handler);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test Verification - Check Requests
    // ─────────────────────────────────────────────────────────────────────────

    // Get all recorded requests
    [[nodiscard]] std::vector<RecordedRequest> requests() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

    // Get number of requests made
    [[nodiscard]] std::size_t request_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_.size();
    }

    // Get last request (or nullopt if none)
    [[nodiscard]] std::optional<RecordedRequest> last_request() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requests_.empty()) {
            return std::nullopt;
        }
        return requests_.back();
    }

    // Check if a specific path was requested
    [[nodiscard]] bool was_requested(const std::string& path) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& req : requests_) {
            if (req.path == path) {
                return true;
            }
        }
        return false;
    }

    // Clear request history
    void clear_requests() {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.clear();
    }

    // Clear response queue
    void clear_responses() {
        std::lock_guard<std::mutex> lock(mutex_);
        response_queue_.clear();
    }

    // Reset everything
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.clear();
        response_queue_.clear();
        response_handler_ = nullptr;
        cancelled_ = false;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration Inspection
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] std::string base_url() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return base_url_;
    }

    [[nodiscard]] HeaderMap default_headers() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return default_headers_;
    }

    [[nodiscard]] std::chrono::milliseconds connect_timeout() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connect_timeout_;
    }

    [[nodiscard]] std::chrono::milliseconds read_timeout() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return read_timeout_;
    }

    [[nodiscard]] bool verify_ssl() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return verify_ssl_;
    }

    [[nodiscard]] bool was_cancelled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cancelled_;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // IHttpClient Implementation
    // ─────────────────────────────────────────────────────────────────────────

    void set_base_url(const std::string& url) override {
        std::lock_guard<std::mutex> lock(mutex_);
        base_url_ = url;
    }

    void set_default_headers(const HeaderMap& headers) override {
        std::lock_guard<std::mutex> lock(mutex_);
        default_headers_ = headers;
    }

    void set_connect_timeout(std::chrono::milliseconds timeout) override {
        std::lock_guard<std::mutex> lock(mutex_);
        connect_timeout_ = timeout;
    }

    void set_read_timeout(std::chrono::milliseconds timeout) override {
        std::lock_guard<std::mutex> lock(mutex_);
        read_timeout_ = timeout;
    }

    void set_verify_ssl(bool verify) override {
        std::lock_guard<std::mutex> lock(mutex_);
        verify_ssl_ = verify;
    }

    HttpClientResult<HttpClientResponse> get(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        return make_request(HttpMethod::Get, path, "", "", headers);
    }

    HttpClientResult<HttpClientResponse> post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers = {}
    ) override {
        return make_request(HttpMethod::Post, path, body, content_type, headers);
    }

    HttpClientResult<HttpClientResponse> del(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        return make_request(HttpMethod::Delete, path, "", "", headers);
    }

    std::future<HttpClientResult<HttpClientResponse>> async_get(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        return std::async(std::launch::deferred, [this, path, headers]() {
            return get(path, headers);
        });
    }

    std::future<HttpClientResult<HttpClientResponse>> async_post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers = {}
    ) override {
        return std::async(std::launch::deferred, [this, path, body, content_type, headers]() {
            return post(path, body, content_type, headers);
        });
    }

    std::future<HttpClientResult<HttpClientResponse>> async_del(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        return std::async(std::launch::deferred, [this, path, headers]() {
            return del(path, headers);
        });
    }

    void cancel() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled_ = true;
    }

private:
    struct QueuedResponse {
        std::optional<HttpClientResponse> result;
        std::optional<HttpClientError> error;
    };

    HttpClientResult<HttpClientResponse> make_request(
        HttpMethod method,
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Record the request
        RecordedRequest req;
        req.method = method;
        req.path = path;
        req.body = body;
        req.content_type = content_type;
        req.headers = headers;
        // Merge default headers
        for (const auto& [k, v] : default_headers_) {
            if (req.headers.find(k) == req.headers.end()) {
                req.headers[k] = v;
            }
        }
        requests_.push_back(req);

        // Check if cancelled
        if (cancelled_) {
            return tl::unexpected(HttpClientError::cancelled());
        }

        // Use handler if set
        if (response_handler_) {
            return response_handler_(method, path, body);
        }

        // Use queued response
        if (response_queue_.empty()) {
            // Default: return 200 OK with empty body
            return HttpClientResponse{200, {}, ""};
        }

        auto queued = std::move(response_queue_.front());
        response_queue_.pop_front();

        if (queued.error.has_value()) {
            return tl::unexpected(*queued.error);
        }

        return *queued.result;
    }

    mutable std::mutex mutex_;
    std::string base_url_;
    HeaderMap default_headers_;
    std::chrono::milliseconds connect_timeout_{10000};
    std::chrono::milliseconds read_timeout_{30000};
    bool verify_ssl_{true};
    bool cancelled_{false};

    std::vector<RecordedRequest> requests_;
    std::deque<QueuedResponse> response_queue_;
    ResponseHandler response_handler_;
};

}  // namespace mcpp::testing

#endif  // MCPP_TESTS_MOCKS_MOCK_HTTP_CLIENT_HPP



