#pragma once

#include "mcpp/transport/backoff_policy.hpp"
#include "mcpp/transport/http_client.hpp"
#include "mcpp/transport/http_transport_config.hpp"
#include "mcpp/transport/http_types.hpp"
#include "mcpp/transport/retry_policy.hpp"
#include "mcpp/transport/session_manager.hpp"
#include "mcpp/transport/sse_parser.hpp"
#include "mcpp/transport/transport_error.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

namespace mcpp {

using Json = nlohmann::json;

// HttpTransportError and HttpResult are defined in transport_error.hpp

// ─────────────────────────────────────────────────────────────────────────────
// HttpTransport
// ─────────────────────────────────────────────────────────────────────────────
// HTTP transport for MCP using the "Streamable HTTP" protocol.
//
// Key features:
// - POST requests with JSON-RPC payloads
// - SSE response streaming
// - Automatic session ID management
// - Thread-safe message queue for received messages
// - Async operations via std::future
//
// The transport uses IHttpClient interface, allowing the underlying HTTP
// implementation to be swapped (cpr, Beast, libcurl, etc.).
//
// Usage:
//   HttpTransportConfig config;
//   config.base_url = "https://api.example.com/mcp";
//   config.with_bearer_token("secret");
//
//   HttpTransport transport(config);
//   transport.start();
//
//   // Send a request (async)
//   Json request = {{"jsonrpc", "2.0"}, {"method", "initialize"}, {"id", 1}};
//   auto future = transport.async_send(request);
//   auto result = future.get();
//
//   // Receive response
//   auto response = transport.receive();
//
//   transport.stop();

class HttpTransport {
public:
    // Construct with config (uses default HTTP client)
    explicit HttpTransport(HttpTransportConfig config);

    // Construct with custom HTTP client (for testing or alternative backends)
    HttpTransport(HttpTransportConfig config, std::unique_ptr<IHttpClient> client);

    ~HttpTransport();

    // Non-copyable, non-movable (owns threads and connections)
    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;
    HttpTransport(HttpTransport&&) = delete;
    HttpTransport& operator=(HttpTransport&&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    // Start the transport (opens SSE stream if configured).
    void start();

    // Stop the transport gracefully.
    // Sends DELETE request if session is active.
    void stop();

    // Check if transport is running.
    [[nodiscard]] bool is_running() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Synchronous Operations
    // ─────────────────────────────────────────────────────────────────────────

    // Send a JSON-RPC message to the server (blocking).
    HttpResult<void> send(const Json& message);

    // Receive the next JSON-RPC message from the server (blocking).
    HttpResult<Json> receive();

    // Receive with timeout.
    HttpResult<std::optional<Json>> receive_with_timeout(
        std::chrono::milliseconds timeout
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Asynchronous Operations
    // ─────────────────────────────────────────────────────────────────────────

    // Send a JSON-RPC message asynchronously.
    [[nodiscard]] std::future<HttpResult<void>> async_send(const Json& message);

    // Receive the next message asynchronously.
    [[nodiscard]] std::future<HttpResult<Json>> async_receive();

    // ─────────────────────────────────────────────────────────────────────────
    // Session Info
    // ─────────────────────────────────────────────────────────────────────────

    // Get current session ID (if server provided one).
    [[nodiscard]] std::optional<std::string> session_id() const;

    // Get current session state.
    [[nodiscard]] SessionState session_state() const;

    // Get the configuration.
    [[nodiscard]] const HttpTransportConfig& config() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Session Events
    // ─────────────────────────────────────────────────────────────────────────

    // Register callback for session state changes.
    void on_session_state_change(SessionManager::StateChangeCallback callback);

    // Register callback when session is established.
    void on_session_established(SessionManager::SessionEstablishedCallback callback);

    // Register callback when session is lost.
    void on_session_lost(SessionManager::SessionLostCallback callback);

private:
    // Internal helpers
    void configure_client();
    void init_retry_components();
    HttpResult<void> do_post(const Json& message);
    HttpResult<void> do_post_with_retry(const Json& message);
    HttpResult<void> handle_session_expired(const Json& original_message);
    bool should_retry(const HttpTransportError& error, std::size_t attempt);
    std::chrono::milliseconds get_retry_delay(std::size_t attempt, const HttpClientResponse* response = nullptr);
    void process_sse_response(const HttpClientResponse& response);
    void process_sse_event(const SseEvent& event);
    void enqueue_message(Json message);
    void sse_reader_loop();
    HeaderMap build_request_headers();

    // Configuration
    HttpTransportConfig config_;
    UrlComponents url_;

    // HTTP client (swappable via IHttpClient interface)
    std::unique_ptr<IHttpClient> http_client_;

    // Session manager (handles state machine and callbacks)
    SessionManager session_manager_;

    // Retry components
    std::shared_ptr<IBackoffPolicy> backoff_policy_;
    std::shared_ptr<RetryPolicy> retry_policy_;

    // Message queue (thread-safe)
    std::queue<Json> message_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // SSE stream reader thread
    std::thread sse_thread_;
    std::atomic<bool> running_{false};

    // SSE parser for incoming events
    SseParser sse_parser_;
};

}  // namespace mcpp

