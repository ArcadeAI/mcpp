#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// Async MCP Client
// ═══════════════════════════════════════════════════════════════════════════
// High-level coroutine-based MCP client.
//
// Usage:
//   asio::io_context io;
//   AsyncMcpClient client(io.get_executor(), config);
//   
//   asio::co_spawn(io, [&]() -> asio::awaitable<void> {
//       co_await client.connect();
//       auto tools = co_await client.list_tools();
//       auto result = co_await client.call_tool("echo", {{"msg", "hi"}});
//       co_await client.disconnect();
//   }, asio::detached);
//   
//   io.run();
//
// Design principles:
// - All I/O is non-blocking (coroutine suspension, not thread blocking)
// - Request/response correlation via pending_requests_ map
// - Concurrent requests supported (each gets unique ID)
// - Notification handling via callbacks or channel

#include "mcpp/async/async_transport.hpp"
#include "mcpp/protocol/mcp_types.hpp"
#include "mcpp/client/client_error.hpp"
#include "mcpp/client/elicitation_handler.hpp"
#include "mcpp/client/async_handlers.hpp"
#include "mcpp/client/sampling_handler.hpp"
#include "mcpp/client/roots_handler.hpp"
#include "mcpp/resilience/circuit_breaker.hpp"

#include <tl/expected.hpp>

#include <asio/awaitable.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace mcpp::async {

// AsyncMcpClientError and AsyncMcpResult are now defined in client_error.hpp
// They are aliased to the shared ClientError and ClientResult types
// (imported via mcpp namespace from client_error.hpp)
using mcpp::AsyncMcpClientError;
using mcpp::AsyncMcpResult;

// ═══════════════════════════════════════════════════════════════════════════
// Async MCP Client Configuration
// ═══════════════════════════════════════════════════════════════════════════

struct AsyncMcpClientConfig {
    /// Client identification
    std::string client_name = "mcpp-async";
    std::string client_version = "0.1.0";

    /// Request timeout (0 = no timeout)
    std::chrono::milliseconds request_timeout{30000};  // 30 seconds

    /// Auto-initialize on connect
    bool auto_initialize = true;

    /// Client capabilities to advertise
    ClientCapabilities capabilities{};
    
    // ─────────────────────────────────────────────────────────────────────────
    // Circuit Breaker Configuration
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Enable circuit breaker for resilience (default: true)
    bool enable_circuit_breaker = true;
    
    /// Circuit breaker configuration
    CircuitBreakerConfig circuit_breaker;
};

// ═══════════════════════════════════════════════════════════════════════════
// Async MCP Client
// ═══════════════════════════════════════════════════════════════════════════

class AsyncMcpClient {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Construction
    // ─────────────────────────────────────────────────────────────────────────

    /// Construct with transport (takes ownership)
    AsyncMcpClient(
        std::unique_ptr<IAsyncTransport> transport,
        AsyncMcpClientConfig config = {}
    );

    ~AsyncMcpClient();

    // Non-copyable, non-movable
    AsyncMcpClient(const AsyncMcpClient&) = delete;
    AsyncMcpClient& operator=(const AsyncMcpClient&) = delete;
    AsyncMcpClient(AsyncMcpClient&&) = delete;
    AsyncMcpClient& operator=(AsyncMcpClient&&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Connection Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    /// Connect and optionally initialize
    [[nodiscard]] asio::awaitable<AsyncMcpResult<InitializeResult>> connect();

    /// Disconnect gracefully
    [[nodiscard]] asio::awaitable<void> disconnect();

    /// Check if connected
    [[nodiscard]] bool is_connected() const;

    /// Check if initialized
    [[nodiscard]] bool is_initialized() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Server Information
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] std::optional<Implementation> server_info() const;
    [[nodiscard]] std::optional<ServerCapabilities> server_capabilities() const;
    [[nodiscard]] std::optional<std::string> server_instructions() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Tools API
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<AsyncMcpResult<ListToolsResult>> list_tools(
        std::optional<std::string> cursor = std::nullopt
    );

    [[nodiscard]] asio::awaitable<AsyncMcpResult<CallToolResult>> call_tool(
        const std::string& name,
        const Json& arguments = {},
        std::optional<ProgressToken> progress_token = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Resources API
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<AsyncMcpResult<ListResourcesResult>> list_resources(
        std::optional<std::string> cursor = std::nullopt
    );

    [[nodiscard]] asio::awaitable<AsyncMcpResult<ReadResourceResult>> read_resource(
        const std::string& uri,
        std::optional<ProgressToken> progress_token = std::nullopt
    );

    /// Subscribe to resource updates (requires server support)
    [[nodiscard]] asio::awaitable<AsyncMcpResult<void>> subscribe_resource(
        const std::string& uri
    );

    /// Unsubscribe from resource updates
    [[nodiscard]] asio::awaitable<AsyncMcpResult<void>> unsubscribe_resource(
        const std::string& uri
    );

    /// List available resource templates (URI templates for dynamic resources)
    [[nodiscard]] asio::awaitable<AsyncMcpResult<ListResourceTemplatesResult>> list_resource_templates(
        std::optional<std::string> cursor = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Prompts API
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<AsyncMcpResult<ListPromptsResult>> list_prompts(
        std::optional<std::string> cursor = std::nullopt
    );

    [[nodiscard]] asio::awaitable<AsyncMcpResult<GetPromptResult>> get_prompt(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& arguments = {},
        std::optional<ProgressToken> progress_token = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Completion API
    // ─────────────────────────────────────────────────────────────────────────

    /// Request autocompletion for a prompt argument
    [[nodiscard]] asio::awaitable<AsyncMcpResult<CompleteResult>> complete_prompt(
        const std::string& prompt_name,
        const std::string& argument_name,
        const std::string& argument_value
    );

    /// Request autocompletion for a resource
    [[nodiscard]] asio::awaitable<AsyncMcpResult<CompleteResult>> complete_resource(
        const std::string& resource_uri,
        const std::string& argument_name,
        const std::string& argument_value
    );

    /// Request autocompletion (generic)
    [[nodiscard]] asio::awaitable<AsyncMcpResult<CompleteResult>> complete(
        const CompleteParams& params
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Logging API
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] asio::awaitable<AsyncMcpResult<void>> set_logging_level(LoggingLevel level);

    // ─────────────────────────────────────────────────────────────────────────
    // Utility Methods
    // ─────────────────────────────────────────────────────────────────────────

    /// Ping the server to check connectivity
    [[nodiscard]] asio::awaitable<AsyncMcpResult<void>> ping();

    /// Cancel an in-progress request
    [[nodiscard]] asio::awaitable<AsyncMcpResult<void>> cancel_request(
        std::variant<std::string, int> request_id,
        std::optional<std::string> reason = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Client Capability Handlers
    // ─────────────────────────────────────────────────────────────────────────

    /// Set handler for elicitation requests from server (sync version)
    void set_elicitation_handler(std::shared_ptr<IElicitationHandler> handler);

    /// Set handler for elicitation requests from server (async version)
    /// Async handlers are preferred over sync handlers when both are set.
    void set_async_elicitation_handler(std::shared_ptr<IAsyncElicitationHandler> handler);

    /// Handle an incoming elicitation request from the server
    [[nodiscard]] asio::awaitable<AsyncMcpResult<Json>> handle_elicitation_request(const Json& params);

    /// Set handler for sampling requests from server (sync version)
    void set_sampling_handler(std::shared_ptr<ISamplingHandler> handler);

    /// Set handler for sampling requests from server (async version)
    /// Async handlers are preferred over sync handlers when both are set.
    void set_async_sampling_handler(std::shared_ptr<IAsyncSamplingHandler> handler);

    /// Handle an incoming sampling/createMessage request from the server
    [[nodiscard]] asio::awaitable<AsyncMcpResult<Json>> handle_sampling_request(const Json& params);

    /// Set handler for roots requests from server (sync version)
    void set_roots_handler(std::shared_ptr<IRootsHandler> handler);

    /// Set handler for roots requests from server (async version)
    /// Async handlers are preferred over sync handlers when both are set.
    void set_async_roots_handler(std::shared_ptr<IAsyncRootsHandler> handler);

    /// Handle an incoming roots/list request from the server
    [[nodiscard]] asio::awaitable<AsyncMcpResult<Json>> handle_roots_list_request();

    /// Notify server that roots have changed (client → server notification)
    [[nodiscard]] asio::awaitable<AsyncMcpResult<void>> notify_roots_changed();

    // ─────────────────────────────────────────────────────────────────────────
    // Notification Handlers
    // ─────────────────────────────────────────────────────────────────────────

    using NotificationHandler = std::function<void(const std::string& method, const Json& params)>;
    
    void on_notification(NotificationHandler handler);
    void on_tool_list_changed(std::function<void()> handler);
    void on_resource_list_changed(std::function<void()> handler);
    void on_resource_updated(std::function<void(const std::string& uri)> handler);
    void on_prompt_list_changed(std::function<void()> handler);
    void on_log_message(std::function<void(LoggingLevel, const std::string&, const std::string&)> handler);
    void on_progress(std::function<void(const ProgressNotification&)> handler);

    // ─────────────────────────────────────────────────────────────────────────
    // Low-Level Access
    // ─────────────────────────────────────────────────────────────────────────

    /// Send a raw JSON-RPC request
    [[nodiscard]] asio::awaitable<AsyncMcpResult<Json>> send_request(
        const std::string& method,
        const Json& params = {}
    );

    /// Send a notification (no response expected)
    [[nodiscard]] asio::awaitable<AsyncMcpResult<void>> send_notification(
        const std::string& method,
        const Json& params = {}
    );

    /// Get the executor
    [[nodiscard]] asio::any_io_executor get_executor();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Circuit Breaker
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Get circuit breaker state
    [[nodiscard]] CircuitState circuit_state() const;
    
    /// Check if circuit is open (requests will fail fast)
    [[nodiscard]] bool is_circuit_open() const;
    
    /// Get circuit breaker statistics
    [[nodiscard]] CircuitBreakerStats circuit_stats() const;
    
    /// Force circuit breaker open (for testing/maintenance)
    void force_circuit_open();
    
    /// Force circuit breaker closed (for testing/recovery)
    void force_circuit_closed();
    
    /// Register callback for circuit state changes
    void on_circuit_state_change(CircuitBreaker::StateChangeCallback callback);

private:
    // Internal types
    struct PendingRequest {
        using ResponseChannel = asio::experimental::channel<
            void(asio::error_code, AsyncMcpResult<Json>)
        >;
        // Use shared_ptr for channel to prevent use-after-free:
        // The timeout handler and async_receive both need the channel to stay alive.
        // If timeout fires while waiting on async_receive, the channel must not be destroyed.
        std::shared_ptr<ResponseChannel> channel;
        std::unique_ptr<asio::steady_timer> timeout_timer;
        
        PendingRequest(asio::any_io_executor exec)
            : channel(std::make_shared<ResponseChannel>(exec, 1))
            , timeout_timer(std::make_unique<asio::steady_timer>(exec))
        {}
    };

    // Internal coroutines
    asio::awaitable<void> message_dispatcher();
    asio::awaitable<void> dispatch_server_request(const Json& request);
    asio::awaitable<void> send_response(const Json& request_id, const AsyncMcpResult<Json>& result);
    asio::awaitable<void> send_error_response(const Json& request_id, int error_code, const std::string& message);
    void dispatch_response(uint64_t id, const Json& response);
    void dispatch_notification(const std::string& method, const Json& params);
    void dispatch_error(uint64_t id, const McpError& error);

    // Helpers
    uint64_t next_request_id();
    AsyncMcpResult<Json> extract_result(const Json& response);

    // Configuration
    AsyncMcpClientConfig config_;

    // Transport
    std::unique_ptr<IAsyncTransport> transport_;

    // Strand for thread-safe access to shared state
    asio::strand<asio::any_io_executor> strand_;

    // State
    std::atomic<bool> connected_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutting_down_{false};  // Set during destruction to prevent use-after-free
    std::atomic<uint64_t> request_id_{0};  // uint64_t to prevent overflow

    // Pending requests (id -> channel) - use uint64_t consistently
    std::unordered_map<uint64_t, std::unique_ptr<PendingRequest>> pending_requests_;

    // Server info
    std::optional<Implementation> server_info_;
    std::optional<ServerCapabilities> server_capabilities_;
    std::optional<std::string> server_instructions_;

    // Notification handlers (protected by notification_handler_mutex_)
    mutable std::mutex notification_handler_mutex_;
    NotificationHandler notification_handler_;
    std::function<void()> tool_list_changed_handler_;
    std::function<void()> resource_list_changed_handler_;
    std::function<void(const std::string&)> resource_updated_handler_;
    std::function<void()> prompt_list_changed_handler_;
    std::function<void(LoggingLevel, const std::string&, const std::string&)> log_message_handler_;
    std::function<void(const ProgressNotification&)> progress_handler_;

    // Capability handlers (sync)
    std::shared_ptr<IElicitationHandler> elicitation_handler_;
    std::shared_ptr<ISamplingHandler> sampling_handler_;
    std::shared_ptr<IRootsHandler> roots_handler_;

    // Capability handlers (async) - preferred over sync when set
    std::shared_ptr<IAsyncElicitationHandler> async_elicitation_handler_;
    std::shared_ptr<IAsyncSamplingHandler> async_sampling_handler_;
    std::shared_ptr<IAsyncRootsHandler> async_roots_handler_;
    
    // Circuit breaker for resilience
    std::unique_ptr<CircuitBreaker> circuit_breaker_;
};

}  // namespace mcpp::async


