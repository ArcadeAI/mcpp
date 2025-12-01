#pragma once

#include "mcpp/protocol/mcp_types.hpp"
#include "mcpp/transport/http_transport.hpp"
#include "mcpp/client/client_error.hpp"
#include "mcpp/client/elicitation_handler.hpp"
#include "mcpp/client/sampling_handler.hpp"
#include "mcpp/client/roots_handler.hpp"
#include "mcpp/resilience/circuit_breaker.hpp"

#include <tl/expected.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace mcpp {

// ═══════════════════════════════════════════════════════════════════════════
// MCP Client Configuration
// ═══════════════════════════════════════════════════════════════════════════

struct McpClientConfig {
    // Client identification
    std::string client_name = "mcpp";
    std::string client_version = "0.1.0";

    // Transport configuration
    HttpTransportConfig transport;

    // Request timeout (0 = no timeout)
    std::chrono::milliseconds request_timeout{30000};
    
    // Handler timeout - max time for handlers (sampling, elicitation, roots) to respond
    // Set to 0 for no timeout (not recommended)
    std::chrono::milliseconds handler_timeout{60000};

    // Auto-initialize on connect
    bool auto_initialize = true;

    // Client capabilities to advertise to server
    ClientCapabilities capabilities;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Circuit Breaker Configuration
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Enable circuit breaker for resilience (default: true)
    bool enable_circuit_breaker = true;
    
    /// Circuit breaker configuration
    CircuitBreakerConfig circuit_breaker;
};

// ═══════════════════════════════════════════════════════════════════════════
// MCP Client
// ═══════════════════════════════════════════════════════════════════════════
// High-level client for interacting with MCP servers.
//
// Usage:
//   McpClientConfig config;
//   config.transport.base_url = "https://api.example.com/mcp";
//   
//   McpClient client(config);
//   client.connect();
//   
//   auto tools = client.list_tools();
//   auto result = client.call_tool("echo", {{"message", "hello"}});
//   
//   client.disconnect();

class McpClient {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Construction
    // ─────────────────────────────────────────────────────────────────────────

    explicit McpClient(McpClientConfig config);
    explicit McpClient(McpClientConfig config, std::unique_ptr<IHttpClient> http_client);
    ~McpClient();

    // Non-copyable, non-movable (due to atomics)
    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;
    McpClient(McpClient&&) = delete;
    McpClient& operator=(McpClient&&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Connection Lifecycle
    // ─────────────────────────────────────────────────────────────────────────

    /// Connect to the MCP server and optionally initialize
    [[nodiscard]] McpResult<InitializeResult> connect();

    /// Disconnect from the server
    void disconnect();

    /// Check if connected
    [[nodiscard]] bool is_connected() const;

    /// Check if initialized (handshake complete)
    [[nodiscard]] bool is_initialized() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Server Information
    // ─────────────────────────────────────────────────────────────────────────

    /// Get server info (available after initialize)
    [[nodiscard]] std::optional<Implementation> server_info() const;

    /// Get server capabilities (available after initialize)
    [[nodiscard]] std::optional<ServerCapabilities> server_capabilities() const;

    /// Get server instructions (available after initialize)
    [[nodiscard]] std::optional<std::string> server_instructions() const;

    // ─────────────────────────────────────────────────────────────────────────
    // Tools API
    // ─────────────────────────────────────────────────────────────────────────

    /// List available tools
    [[nodiscard]] McpResult<ListToolsResult> list_tools(std::optional<std::string> cursor = std::nullopt);

    /// Call a tool with optional progress token
    [[nodiscard]] McpResult<CallToolResult> call_tool(
        const std::string& name,
        const Json& arguments = {},
        std::optional<ProgressToken> progress_token = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Resources API
    // ─────────────────────────────────────────────────────────────────────────

    /// List available resources
    [[nodiscard]] McpResult<ListResourcesResult> list_resources(std::optional<std::string> cursor = std::nullopt);

    /// Read a resource with optional progress token
    [[nodiscard]] McpResult<ReadResourceResult> read_resource(
        const std::string& uri,
        std::optional<ProgressToken> progress_token = std::nullopt
    );

    /// Subscribe to resource updates (requires server support)
    [[nodiscard]] McpResult<void> subscribe_resource(const std::string& uri);

    /// Unsubscribe from resource updates
    [[nodiscard]] McpResult<void> unsubscribe_resource(const std::string& uri);

    /// List available resource templates (URI templates for dynamic resources)
    [[nodiscard]] McpResult<ListResourceTemplatesResult> list_resource_templates(
        std::optional<std::string> cursor = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Prompts API
    // ─────────────────────────────────────────────────────────────────────────

    /// List available prompts
    [[nodiscard]] McpResult<ListPromptsResult> list_prompts(std::optional<std::string> cursor = std::nullopt);

    /// Get a prompt with arguments and optional progress token
    [[nodiscard]] McpResult<GetPromptResult> get_prompt(
        const std::string& name,
        const std::unordered_map<std::string, std::string>& arguments = {},
        std::optional<ProgressToken> progress_token = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Completion API
    // ─────────────────────────────────────────────────────────────────────────

    /// Request autocompletion for a prompt argument
    McpResult<CompleteResult> complete_prompt(
        const std::string& prompt_name,
        const std::string& argument_name,
        const std::string& argument_value
    );

    /// Request autocompletion for a resource
    McpResult<CompleteResult> complete_resource(
        const std::string& resource_uri,
        const std::string& argument_name,
        const std::string& argument_value
    );

    /// Request autocompletion (generic)
    McpResult<CompleteResult> complete(const CompleteParams& params);

    // ─────────────────────────────────────────────────────────────────────────
    // Logging API
    // ─────────────────────────────────────────────────────────────────────────

    /// Set the logging level for the server
    McpResult<void> set_logging_level(LoggingLevel level);

    // ─────────────────────────────────────────────────────────────────────────
    // Utility Methods
    // ─────────────────────────────────────────────────────────────────────────

    /// Ping the server to check connectivity
    McpResult<void> ping();

    /// Cancel an in-progress request
    McpResult<void> cancel_request(
        std::variant<std::string, int> request_id,
        std::optional<std::string> reason = std::nullopt
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Client Capability Handlers
    // ─────────────────────────────────────────────────────────────────────────

    /// Set handler for elicitation requests from server
    void set_elicitation_handler(std::shared_ptr<IElicitationHandler> handler);

    /// Handle an incoming elicitation request from the server
    /// Returns the result JSON to send back to the server
    McpResult<Json> handle_elicitation_request(const Json& params);

    /// Set handler for sampling requests from server
    void set_sampling_handler(std::shared_ptr<ISamplingHandler> handler);

    /// Handle an incoming sampling/createMessage request from the server
    /// Returns the result JSON to send back to the server
    McpResult<Json> handle_sampling_request(const Json& params);

    /// Set handler for roots requests from server
    void set_roots_handler(std::shared_ptr<IRootsHandler> handler);

    /// Handle an incoming roots/list request from the server
    /// Returns the result JSON to send back to the server
    McpResult<Json> handle_roots_list_request();

    /// Notify server that roots have changed (client → server notification)
    McpResult<void> notify_roots_changed();

    // ─────────────────────────────────────────────────────────────────────────
    // Event Handlers
    // ─────────────────────────────────────────────────────────────────────────

    using NotificationHandler = std::function<void(const std::string& method, const Json& params)>;
    using ToolListChangedCallback = std::function<void()>;
    using ResourceListChangedCallback = std::function<void()>;
    using PromptListChangedCallback = std::function<void()>;
    using LogMessageCallback = std::function<void(LoggingLevel level, const std::string& logger, const std::string& data)>;
    using ProgressCallback = std::function<void(const ProgressNotification&)>;

    void on_notification(NotificationHandler handler);
    void on_tool_list_changed(ToolListChangedCallback callback);
    void on_resource_list_changed(ResourceListChangedCallback callback);
    void on_resource_updated(std::function<void(const std::string&)> callback);
    void on_prompt_list_changed(PromptListChangedCallback callback);
    void on_log_message(LogMessageCallback callback);
    void on_progress(ProgressCallback callback);

    // ─────────────────────────────────────────────────────────────────────────
    // Low-Level Access
    // ─────────────────────────────────────────────────────────────────────────

    /// Send a raw JSON-RPC request and get the response
    McpResult<Json> send_request(const std::string& method, const Json& params = {});

    /// Send a notification (no response expected)
    McpResult<void> send_notification(const std::string& method, const Json& params = {});

    /// Get the underlying transport
    HttpTransport& transport();
    
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
    // Internal helpers
    McpResult<Json> send_and_receive(const Json& request);
    McpResult<Json> extract_result(const Json& response);
    void handle_server_request(const Json& request);
    void send_response(const Json& request_id, const McpResult<Json>& result);
    void dispatch_notification(const Json& message);
    uint64_t next_request_id();

    // Configuration
    McpClientConfig config_;

    // Transport
    std::unique_ptr<HttpTransport> transport_;

    // State
    std::atomic<bool> connected_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<uint64_t> request_id_{0};  // uint64_t to prevent overflow

    // Server info (populated after initialize)
    std::optional<Implementation> server_info_;
    std::optional<ServerCapabilities> server_capabilities_;
    std::optional<std::string> server_instructions_;

    // Event callbacks
    NotificationHandler notification_handler_;
    ToolListChangedCallback tool_list_changed_handler_;
    ResourceListChangedCallback resource_list_changed_handler_;
    std::function<void(const std::string&)> resource_updated_handler_;
    PromptListChangedCallback prompt_list_changed_handler_;
    LogMessageCallback log_message_handler_;
    ProgressCallback progress_handler_;

    // Capability handlers
    std::shared_ptr<IElicitationHandler> elicitation_handler_;
    std::shared_ptr<ISamplingHandler> sampling_handler_;
    std::shared_ptr<IRootsHandler> roots_handler_;
    
    // Circuit breaker for resilience
    std::unique_ptr<CircuitBreaker> circuit_breaker_;
};

}  // namespace mcpp


