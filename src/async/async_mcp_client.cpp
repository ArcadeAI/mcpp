#include "mcpp/async/async_mcp_client.hpp"
#include "mcpp/client/handler_utils.hpp"
#include "mcpp/log/logger.hpp"
#include "mcpp/security/url_validator.hpp"

#include <asio/bind_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/dispatch.hpp>

namespace mcpp::async {

// ═══════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════

AsyncMcpClient::AsyncMcpClient(
    std::unique_ptr<IAsyncTransport> transport,
    AsyncMcpClientConfig config
)
    : config_(std::move(config))
    , transport_(std::move(transport))
    , strand_(asio::make_strand(transport_->get_executor()))
{
    if (config_.enable_circuit_breaker) {
        circuit_breaker_ = std::make_unique<CircuitBreaker>(config_.circuit_breaker);
    }
}

AsyncMcpClient::~AsyncMcpClient() {
    // CRITICAL: Set shutdown flag FIRST to prevent use-after-free in async handlers
    // Any pending timeout handlers will check this flag and bail out
    shutting_down_.store(true, std::memory_order_release);
    
    // Cancel all pending timers to reduce chance of handlers firing
    for (auto& [id, req] : pending_requests_) {
        if (req && req->timeout_timer) {
            req->timeout_timer->cancel();
        }
    }
    
    // Note: Can't co_await in destructor
    // User should call disconnect() before destroying
}

// ═══════════════════════════════════════════════════════════════════════════
// Connection Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<AsyncMcpResult<InitializeResult>> AsyncMcpClient::connect() {
    if (connected_) {
        co_return tl::unexpected(AsyncMcpClientError::protocol_error("Already connected"));
    }

    // Start transport
    auto start_result = co_await transport_->async_start();
    if (!start_result) {
        co_return tl::unexpected(AsyncMcpClientError::transport_error(start_result.error().message));
    }

    connected_ = true;

    // Spawn message dispatcher
    asio::co_spawn(strand_, message_dispatcher(), asio::detached);

    if (config_.auto_initialize) {
        // Build initialize params
        InitializeParams params;
        params.protocol_version = MCP_PROTOCOL_VERSION;
        params.client_info = {config_.client_name, config_.client_version};
        params.capabilities = config_.capabilities;

        // Send initialize request
        auto result = co_await send_request("initialize", params.to_json());
        if (!result) {
            co_await disconnect();
            co_return tl::unexpected(result.error());
        }

        // Parse response
        auto init_result = InitializeResult::from_json(*result);
        server_info_ = init_result.server_info;
        server_capabilities_ = init_result.capabilities;
        server_instructions_ = init_result.instructions;

        // Send initialized notification
        auto notify_result = co_await send_notification("notifications/initialized");
        if (!notify_result) {
            co_await disconnect();
            co_return tl::unexpected(notify_result.error());
        }

        initialized_ = true;
        MCPP_LOG_INFO("Async MCP client initialized");

        co_return init_result;
    }

    co_return InitializeResult{};
}

asio::awaitable<void> AsyncMcpClient::disconnect() {
    if (!connected_) {
        co_return;
    }

    // Ensure we're on the strand before accessing shared state
    // This is critical for thread safety when disconnect() is called from
    // a different context than the message_dispatcher()
    co_await asio::dispatch(asio::bind_executor(strand_, asio::use_awaitable));

    // Cancel all pending requests
    for (auto& [id, req] : pending_requests_) {
        req->timeout_timer->cancel();
        req->channel->close();
    }
    pending_requests_.clear();

    // Stop transport
    co_await transport_->async_stop();

    connected_ = false;
    initialized_ = false;
    server_info_.reset();
    server_capabilities_.reset();
    server_instructions_.reset();

    MCPP_LOG_INFO("Async MCP client disconnected");
}

bool AsyncMcpClient::is_connected() const {
    return connected_;
}

bool AsyncMcpClient::is_initialized() const {
    return initialized_;
}

// ═══════════════════════════════════════════════════════════════════════════
// Server Information
// ═══════════════════════════════════════════════════════════════════════════

std::optional<Implementation> AsyncMcpClient::server_info() const {
    return server_info_;
}

std::optional<ServerCapabilities> AsyncMcpClient::server_capabilities() const {
    return server_capabilities_;
}

std::optional<std::string> AsyncMcpClient::server_instructions() const {
    return server_instructions_;
}

// ═══════════════════════════════════════════════════════════════════════════
// Tools API
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<AsyncMcpResult<ListToolsResult>> AsyncMcpClient::list_tools(
    std::optional<std::string> cursor
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = co_await send_request("tools/list", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return ListToolsResult::from_json(*result);
}

asio::awaitable<AsyncMcpResult<CallToolResult>> AsyncMcpClient::call_tool(
    const std::string& name,
    const Json& arguments,
    std::optional<ProgressToken> progress_token
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    CallToolParams params;
    params.name = name;
    params.arguments = arguments;
    
    if (progress_token) {
        params.meta = RequestMeta{.progress_token = std::move(progress_token)};
    }

    auto result = co_await send_request("tools/call", params.to_json());
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return CallToolResult::from_json(*result);
}

// ═══════════════════════════════════════════════════════════════════════════
// Resources API
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<AsyncMcpResult<ListResourcesResult>> AsyncMcpClient::list_resources(
    std::optional<std::string> cursor
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = co_await send_request("resources/list", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return ListResourcesResult::from_json(*result);
}

asio::awaitable<AsyncMcpResult<ReadResourceResult>> AsyncMcpClient::read_resource(
    const std::string& uri,
    std::optional<ProgressToken> progress_token
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = {{"uri", uri}};
    
    if (progress_token) {
        RequestMeta meta{.progress_token = std::move(progress_token)};
        params["_meta"] = meta.to_json();
    }

    auto result = co_await send_request("resources/read", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return ReadResourceResult::from_json(*result);
}

asio::awaitable<AsyncMcpResult<void>> AsyncMcpClient::subscribe_resource(
    const std::string& uri
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }
    
    // Check if server supports subscriptions
    if (server_capabilities_ && server_capabilities_->resources) {
        if (!server_capabilities_->resources->subscribe) {
            co_return tl::unexpected(AsyncMcpClientError{
                ClientErrorCode::ProtocolError,
                "Server does not support resource subscriptions",
                std::nullopt
            });
        }
    }

    Json params = {{"uri", uri}};

    auto result = co_await send_request("resources/subscribe", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return AsyncMcpResult<void>{};
}

asio::awaitable<AsyncMcpResult<void>> AsyncMcpClient::unsubscribe_resource(
    const std::string& uri
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = {{"uri", uri}};

    auto result = co_await send_request("resources/unsubscribe", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return AsyncMcpResult<void>{};
}

asio::awaitable<AsyncMcpResult<ListResourceTemplatesResult>> AsyncMcpClient::list_resource_templates(
    std::optional<std::string> cursor
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = co_await send_request("resources/templates/list", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return ListResourceTemplatesResult::from_json(*result);
}

// ═══════════════════════════════════════════════════════════════════════════
// Prompts API
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<AsyncMcpResult<ListPromptsResult>> AsyncMcpClient::list_prompts(
    std::optional<std::string> cursor
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = co_await send_request("prompts/list", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return ListPromptsResult::from_json(*result);
}

asio::awaitable<AsyncMcpResult<GetPromptResult>> AsyncMcpClient::get_prompt(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& arguments,
    std::optional<ProgressToken> progress_token
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = {{"name", name}};
    if (!arguments.empty()) {
        params["arguments"] = arguments;
    }
    
    if (progress_token) {
        RequestMeta meta{.progress_token = std::move(progress_token)};
        params["_meta"] = meta.to_json();
    }

    auto result = co_await send_request("prompts/get", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return GetPromptResult::from_json(*result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Completion API
// ─────────────────────────────────────────────────────────────────────────────

asio::awaitable<AsyncMcpResult<CompleteResult>> AsyncMcpClient::complete_prompt(
    const std::string& prompt_name,
    const std::string& argument_name,
    const std::string& argument_value
) {
    CompleteParams params;
    params.ref = CompletionReference{CompletionRefType::Prompt, prompt_name};
    params.argument = CompletionArgument{argument_name, argument_value};
    co_return co_await complete(params);
}

asio::awaitable<AsyncMcpResult<CompleteResult>> AsyncMcpClient::complete_resource(
    const std::string& resource_uri,
    const std::string& argument_name,
    const std::string& argument_value
) {
    CompleteParams params;
    params.ref = CompletionReference{CompletionRefType::Resource, resource_uri};
    params.argument = CompletionArgument{argument_name, argument_value};
    co_return co_await complete(params);
}

asio::awaitable<AsyncMcpResult<CompleteResult>> AsyncMcpClient::complete(
    const CompleteParams& params
) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    auto result = co_await send_request("completion/complete", params.to_json());
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return CompleteResult::from_json(*result);
}

// ═══════════════════════════════════════════════════════════════════════════
// Logging API
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<AsyncMcpResult<void>> AsyncMcpClient::set_logging_level(LoggingLevel level) {
    if (!initialized_) {
        co_return tl::unexpected(AsyncMcpClientError::not_initialized());
    }

    Json params = {{"level", to_string(level)}};

    auto result = co_await send_request("logging/setLevel", params);
    if (!result) {
        co_return tl::unexpected(result.error());
    }

    co_return AsyncMcpResult<void>{};
}

// ═══════════════════════════════════════════════════════════════════════════
// Notification Handlers
// ═══════════════════════════════════════════════════════════════════════════

void AsyncMcpClient::on_notification(NotificationHandler handler) {
    std::lock_guard<std::mutex> lock(notification_handler_mutex_);
    notification_handler_ = std::move(handler);
}

void AsyncMcpClient::on_tool_list_changed(std::function<void()> handler) {
    std::lock_guard<std::mutex> lock(notification_handler_mutex_);
    tool_list_changed_handler_ = std::move(handler);
}

void AsyncMcpClient::on_resource_list_changed(std::function<void()> handler) {
    std::lock_guard<std::mutex> lock(notification_handler_mutex_);
    resource_list_changed_handler_ = std::move(handler);
}

void AsyncMcpClient::on_resource_updated(std::function<void(const std::string&)> handler) {
    std::lock_guard<std::mutex> lock(notification_handler_mutex_);
    resource_updated_handler_ = std::move(handler);
}

void AsyncMcpClient::on_prompt_list_changed(std::function<void()> handler) {
    std::lock_guard<std::mutex> lock(notification_handler_mutex_);
    prompt_list_changed_handler_ = std::move(handler);
}

void AsyncMcpClient::on_log_message(
    std::function<void(LoggingLevel, const std::string&, const std::string&)> handler
) {
    std::lock_guard<std::mutex> lock(notification_handler_mutex_);
    log_message_handler_ = std::move(handler);
}

void AsyncMcpClient::on_progress(std::function<void(const ProgressNotification&)> handler) {
    std::lock_guard<std::mutex> lock(notification_handler_mutex_);
    progress_handler_ = std::move(handler);
}

// ═══════════════════════════════════════════════════════════════════════════
// Utility Methods
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<AsyncMcpResult<void>> AsyncMcpClient::ping() {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }
    
    auto result = co_await send_request("ping");
    if (!result) {
        co_return tl::unexpected(result.error());
    }
    
    co_return AsyncMcpResult<void>{};
}

asio::awaitable<AsyncMcpResult<void>> AsyncMcpClient::cancel_request(
    std::variant<std::string, int> request_id,
    std::optional<std::string> reason
) {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }
    
    CancelledNotification notification;
    notification.request_id = std::move(request_id);
    notification.reason = std::move(reason);
    
    co_return co_await send_notification("notifications/cancelled", notification.to_json());
}

// ═══════════════════════════════════════════════════════════════════════════
// Client Capability Handlers
// ═══════════════════════════════════════════════════════════════════════════

void AsyncMcpClient::set_elicitation_handler(std::shared_ptr<IElicitationHandler> handler) {
    elicitation_handler_ = std::move(handler);
}

void AsyncMcpClient::set_async_elicitation_handler(std::shared_ptr<IAsyncElicitationHandler> handler) {
    async_elicitation_handler_ = std::move(handler);
}

asio::awaitable<AsyncMcpResult<Json>> AsyncMcpClient::handle_elicitation_request(const Json& params) {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }
    
    const std::string mode = get_elicitation_mode(params);
    ElicitationResult result;
    
    if (is_url_elicitation(mode)) {
        auto url_params = UrlElicitationParams::from_json(params);
        
        // Validate URL using shared security validation
        auto validation = validate_elicitation_url(url_params.url);
        if (validation.should_decline) {
            MCPP_LOG_WARN("Rejecting unsafe elicitation URL: " + url_params.url + 
                          " - Reason: " + validation.decline_reason);
            co_return ElicitationResult{ElicitationAction::Decline, std::nullopt}.to_json();
        }
        if (validation.warning) {
            MCPP_LOG_WARN("Elicitation URL warning: " + *validation.warning);
        }
        
        // Prefer async handler over sync handler
        if (async_elicitation_handler_) {
            result = co_await async_elicitation_handler_->handle_url_async(
                url_params.elicitation_id,
                url_params.url,
                url_params.message
            );
        } else if (elicitation_handler_) {
            result = elicitation_handler_->handle_url(
                url_params.elicitation_id,
                url_params.url,
                url_params.message
            );
        } else {
            result = {ElicitationAction::Dismiss, std::nullopt};
        }
    } else {
        // Form mode - no URL validation needed
        auto form_params = FormElicitationParams::from_json(params);
        
        if (async_elicitation_handler_) {
            result = co_await async_elicitation_handler_->handle_form_async(
                form_params.message,
                form_params.requested_schema
            );
        } else if (elicitation_handler_) {
            result = elicitation_handler_->handle_form(
                form_params.message,
                form_params.requested_schema
            );
        } else {
            result = {ElicitationAction::Dismiss, std::nullopt};
        }
    }
    
    co_return result.to_json();
}

void AsyncMcpClient::set_sampling_handler(std::shared_ptr<ISamplingHandler> handler) {
    sampling_handler_ = std::move(handler);
}

void AsyncMcpClient::set_async_sampling_handler(std::shared_ptr<IAsyncSamplingHandler> handler) {
    async_sampling_handler_ = std::move(handler);
}

asio::awaitable<AsyncMcpResult<Json>> AsyncMcpClient::handle_sampling_request(const Json& params) {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }
    
    auto create_params = CreateMessageParams::from_json(params);
    std::optional<CreateMessageResult> result;
    
    // Prefer async handler over sync handler
    if (async_sampling_handler_) {
        result = co_await async_sampling_handler_->handle_create_message_async(create_params);
    } else if (sampling_handler_) {
        result = sampling_handler_->handle_create_message(create_params);
    } else {
        co_return tl::unexpected(AsyncMcpClientError::protocol_error("No sampling handler configured"));
    }
    
    if (!result) {
        co_return tl::unexpected(AsyncMcpClientError::protocol_error("Sampling request declined by handler"));
    }
    
    co_return result->to_json();
}

void AsyncMcpClient::set_roots_handler(std::shared_ptr<IRootsHandler> handler) {
    roots_handler_ = std::move(handler);
}

void AsyncMcpClient::set_async_roots_handler(std::shared_ptr<IAsyncRootsHandler> handler) {
    async_roots_handler_ = std::move(handler);
}

asio::awaitable<AsyncMcpResult<Json>> AsyncMcpClient::handle_roots_list_request() {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }
    
    ListRootsResult result;
    
    // Prefer async handler over sync handler
    if (async_roots_handler_) {
        result = co_await async_roots_handler_->list_roots_async();
    } else if (roots_handler_) {
        result = roots_handler_->list_roots();
    }
    
    co_return result.to_json();
}

asio::awaitable<AsyncMcpResult<void>> AsyncMcpClient::notify_roots_changed() {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }
    co_return co_await send_notification("notifications/roots/list_changed");
}

// ═══════════════════════════════════════════════════════════════════════════
// Low-Level Access
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<AsyncMcpResult<Json>> AsyncMcpClient::send_request(
    const std::string& method,
    const Json& params
) {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }
    
    // Check circuit breaker before making request
    if (circuit_breaker_ && !circuit_breaker_->allow_request()) {
        co_return tl::unexpected(AsyncMcpClientError{
            ClientErrorCode::TransportError,
            "Circuit breaker is open - server appears unhealthy",
            std::nullopt
        });
    }

    uint64_t id = next_request_id();

    // Build request
    Json request = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method}
    };
    if (!params.empty()) {
        request["params"] = params;
    }

    // Create pending request with response channel
    auto pending = std::make_unique<PendingRequest>(transport_->get_executor());
    pending_requests_[id] = std::move(pending);
    
    // Get reference to the pending request (safe because we own it until erase)
    auto& req = pending_requests_[id];

    // Set timeout if configured
    // CRITICAL: Check shutting_down_ flag to prevent use-after-free.
    // If client is being destroyed, the handler must not access any members.
    if (config_.request_timeout.count() > 0) {
        req->timeout_timer->expires_after(config_.request_timeout);
        req->timeout_timer->async_wait(asio::bind_executor(strand_,
            [this, id](asio::error_code ec) {
                // Check if client is shutting down to prevent use-after-free
                if (shutting_down_.load(std::memory_order_acquire)) {
                    return;  // Client being destroyed, don't access members
                }
                if (ec) {
                    return;  // Timer cancelled or error
                }
                // Look up request - if gone, response already arrived
                auto it = pending_requests_.find(id);
                if (it == pending_requests_.end()) {
                    return;  // Request already completed
                }
                // Timeout - record failure and send error to channel
                if (circuit_breaker_) {
                    circuit_breaker_->record_failure();
                }
                it->second->channel->try_send(
                    asio::error_code{},
                    tl::unexpected(AsyncMcpClientError::timeout_error())
                );
                pending_requests_.erase(it);
            }
        ));
    }

    // Send request
    auto send_result = co_await transport_->async_send(std::move(request));
    if (!send_result) {
        if (circuit_breaker_) {
            circuit_breaker_->record_failure();
        }
        if (auto it = pending_requests_.find(id); it != pending_requests_.end()) {
            it->second->timeout_timer->cancel();
            pending_requests_.erase(it);
        }
        co_return tl::unexpected(AsyncMcpClientError::transport_error(send_result.error().message));
    }

    // Wait for response
    // Keep shared_ptr to channel alive during await - prevents use-after-free
    // if timeout handler fires and erases the request from pending_requests_
    auto channel = req->channel;  // Copy shared_ptr
    try {
        auto result = co_await channel->async_receive(asio::use_awaitable);
        // Clean up (timer might still be pending, cancel it)
        if (auto it = pending_requests_.find(id); it != pending_requests_.end()) {
            it->second->timeout_timer->cancel();
            pending_requests_.erase(it);
        }
        // Record success on successful response
        if (result.has_value() && circuit_breaker_) {
            circuit_breaker_->record_success();
        }
        co_return result;
    } catch (const std::system_error& e) {
        if (circuit_breaker_) {
            circuit_breaker_->record_failure();
        }
        if (auto it = pending_requests_.find(id); it != pending_requests_.end()) {
            pending_requests_.erase(it);
        }
        co_return tl::unexpected(AsyncMcpClientError::transport_error(e.what()));
    }
}

asio::awaitable<AsyncMcpResult<void>> AsyncMcpClient::send_notification(
    const std::string& method,
    const Json& params
) {
    if (!connected_) {
        co_return tl::unexpected(AsyncMcpClientError::not_connected());
    }

    Json notification = {
        {"jsonrpc", "2.0"},
        {"method", method}
    };
    if (!params.empty()) {
        notification["params"] = params;
    }

    auto result = co_await transport_->async_send(std::move(notification));
    if (!result) {
        co_return tl::unexpected(AsyncMcpClientError::transport_error(result.error().message));
    }

    co_return AsyncMcpResult<void>{};
}

asio::any_io_executor AsyncMcpClient::get_executor() {
    return transport_->get_executor();
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: Message Dispatcher
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<void> AsyncMcpClient::message_dispatcher() {
    while (connected_) {
        auto result = co_await transport_->async_receive();

        if (!result) {
            // Transport error - disconnect
            if (connected_) {
                MCPP_LOG_ERROR("Transport error: " + result.error().message);
            }
            break;
        }

        const Json& message = *result;

        // Check message type based on JSON-RPC 2.0 spec:
        // - Request: has "method" AND "id"
        // - Notification: has "method" but NO "id"
        // - Response: has "id" but NO "method"

        bool has_id = message.contains("id") && !message["id"].is_null();
        bool has_method = message.contains("method");

        if (has_method && has_id) {
            // Server request - dispatch and respond
            co_await dispatch_server_request(message);
        }
        else if (has_id) {
            // Response to our request
            // JSON-RPC 2.0 allows string or integer IDs - we use uint64_t internally
            // Handle both signed and unsigned integer IDs from servers
            const auto& id_json = message["id"];
            if (id_json.is_number_unsigned()) {
                uint64_t id = id_json.get<uint64_t>();
                if (message.contains("error")) {
                    auto error = McpError::from_json(message["error"]);
                    dispatch_error(id, error);
                } else {
                    dispatch_response(id, message);
                }
            } else if (id_json.is_number_integer()) {
                // Handle signed integers (convert to uint64_t)
                int64_t signed_id = id_json.get<int64_t>();
                if (signed_id >= 0) {
                    uint64_t id = static_cast<uint64_t>(signed_id);
                    if (message.contains("error")) {
                        auto error = McpError::from_json(message["error"]);
                        dispatch_error(id, error);
                    } else {
                        dispatch_response(id, message);
                    }
                } else {
                    MCPP_LOG_WARN("Received response with negative ID, ignoring");
                }
            } else {
                // String ID - log warning and ignore (we only use integer IDs)
                MCPP_LOG_WARN("Received response with non-integer ID, ignoring");
            }
        }
        else if (has_method) {
            // Notification from server
            std::string method = message["method"].get<std::string>();
            Json params = message.value("params", Json::object());
            dispatch_notification(method, params);
        }
    }
}

asio::awaitable<void> AsyncMcpClient::dispatch_server_request(const Json& request) {
    std::string method = request["method"].get<std::string>();
    Json params = request.value("params", Json::object());
    auto request_id = request["id"];
    
    MCPP_LOG_DEBUG("Handling server request: " + method);
    
    // Route to appropriate handler based on method
    // All handlers are now coroutines to support async handlers
    AsyncMcpResult<Json> result;
    
    if (method == "elicitation/create") {
        result = co_await handle_elicitation_request(params);
    }
    else if (method == "sampling/createMessage") {
        result = co_await handle_sampling_request(params);
    }
    else if (method == "roots/list") {
        result = co_await handle_roots_list_request();
    }
    else {
        // Unknown method - send error response with standard JSON-RPC code
        co_await send_error_response(request_id, ErrorCode::MethodNotFound, 
            "Method not found: " + method);
        co_return;
    }
    
    // Send response back to server
    co_await send_response(request_id, result);
}

asio::awaitable<void> AsyncMcpClient::send_response(
    const Json& request_id,
    const AsyncMcpResult<Json>& result
) {
    Json response = {
        {"jsonrpc", "2.0"},
        {"id", request_id}
    };
    
    if (result.has_value()) {
        response["result"] = *result;
    } else {
        // Map internal error codes to JSON-RPC error codes
        int error_code = ErrorCode::InternalError;  // Default
        if (result.error().message.find("No") != std::string::npos &&
            result.error().message.find("handler") != std::string::npos) {
            error_code = ErrorCode::InternalError;
        }
        
        response["error"] = {
            {"code", error_code},
            {"message", result.error().message}
        };
    }
    
    auto send_result = co_await transport_->async_send(std::move(response));
    if (!send_result) {
        MCPP_LOG_ERROR("Failed to send response: " + send_result.error().message);
    }
}

asio::awaitable<void> AsyncMcpClient::send_error_response(
    const Json& request_id,
    int error_code,
    const std::string& message
) {
    Json response = {
        {"jsonrpc", "2.0"},
        {"id", request_id},
        {"error", {
            {"code", error_code},
            {"message", message}
        }}
    };
    
    auto send_result = co_await transport_->async_send(std::move(response));
    if (!send_result) {
        MCPP_LOG_ERROR("Failed to send error response: " + send_result.error().message);
    }
}

void AsyncMcpClient::dispatch_response(uint64_t id, const Json& response) {
    auto it = pending_requests_.find(id);
    if (it == pending_requests_.end()) {
        MCPP_LOG_WARN("Received response for unknown request ID: " + std::to_string(id));
        return;
    }

    auto result = extract_result(response);
    it->second->channel->try_send(asio::error_code{}, std::move(result));
}

void AsyncMcpClient::dispatch_error(uint64_t id, const McpError& error) {
    auto it = pending_requests_.find(id);
    if (it == pending_requests_.end()) {
        MCPP_LOG_WARN("Received error for unknown request ID: " + std::to_string(id));
        return;
    }

    it->second->channel->try_send(
        asio::error_code{},
        tl::unexpected(AsyncMcpClientError::from_rpc_error(error))
    );
}

void AsyncMcpClient::dispatch_notification(const std::string& method, const Json& params) {
    // Copy handlers under lock to avoid holding lock during callback execution
    // This prevents deadlocks if callback tries to set a new handler
    NotificationHandler generic_handler;
    std::function<void()> tool_changed;
    std::function<void()> resource_changed;
    std::function<void(const std::string&)> resource_updated;
    std::function<void()> prompt_changed;
    std::function<void(LoggingLevel, const std::string&, const std::string&)> log_message;
    std::function<void(const ProgressNotification&)> progress;
    
    {
        std::lock_guard<std::mutex> lock(notification_handler_mutex_);
        generic_handler = notification_handler_;
        tool_changed = tool_list_changed_handler_;
        resource_changed = resource_list_changed_handler_;
        resource_updated = resource_updated_handler_;
        prompt_changed = prompt_list_changed_handler_;
        log_message = log_message_handler_;
        progress = progress_handler_;
    }
    
    // Helper to safely invoke handlers with exception protection
    auto safe_invoke = [](auto&& handler, auto&&... args) {
        try {
            if (handler) {
                handler(std::forward<decltype(args)>(args)...);
            }
        } catch (const std::exception& e) {
            MCPP_LOG_ERROR("Exception in notification handler: " + std::string(e.what()));
        } catch (...) {
            MCPP_LOG_ERROR("Unknown exception in notification handler");
        }
    };
    
    // Call generic handler first
    safe_invoke(generic_handler, method, params);

    // Dispatch based on method
    if (method == "notifications/tools/list_changed") {
        safe_invoke(tool_changed);
    }
    else if (method == "notifications/resources/list_changed") {
        safe_invoke(resource_changed);
    }
    else if (method == "notifications/resources/updated") {
        if (resource_updated) {
            try {
                auto notification = ResourceUpdatedNotification::from_json(params);
                safe_invoke(resource_updated, notification.uri);
            } catch (const std::exception& e) {
                MCPP_LOG_ERROR("Failed to parse resource updated notification: " + std::string(e.what()));
            }
        }
    }
    else if (method == "notifications/prompts/list_changed") {
        safe_invoke(prompt_changed);
    }
    else if (method == "notifications/message") {
        if (log_message) {
            try {
                std::string level_str = params.value("level", "info");
                LoggingLevel level = logging_level_from_string(level_str);
                auto logger_name = params.value("logger", "");
                auto data = params.value("data", "");
                safe_invoke(log_message, level, logger_name, data);
            } catch (const std::exception& e) {
                MCPP_LOG_ERROR("Failed to parse log message notification: " + std::string(e.what()));
            }
        }
    }
    else if (method == "notifications/progress") {
        if (progress) {
            try {
                auto prog = ProgressNotification::from_json(params);
                safe_invoke(progress, prog);
            } catch (const std::exception& e) {
                MCPP_LOG_ERROR("Failed to parse progress notification: " + std::string(e.what()));
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: Helpers
// ═══════════════════════════════════════════════════════════════════════════

uint64_t AsyncMcpClient::next_request_id() {
    return ++request_id_;
}

AsyncMcpResult<Json> AsyncMcpClient::extract_result(const Json& response) {
    if (response.contains("error")) {
        auto error = McpError::from_json(response["error"]);
        return tl::unexpected(AsyncMcpClientError::from_rpc_error(error));
    }

    if (response.contains("result")) {
        return response["result"];
    }

    return tl::unexpected(AsyncMcpClientError::protocol_error("Response missing 'result' field"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Circuit Breaker
// ─────────────────────────────────────────────────────────────────────────────

CircuitState AsyncMcpClient::circuit_state() const {
    if (!circuit_breaker_) {
        return CircuitState::Closed;  // No breaker = always closed
    }
    return circuit_breaker_->state();
}

bool AsyncMcpClient::is_circuit_open() const {
    if (!circuit_breaker_) {
        return false;
    }
    return circuit_breaker_->is_open();
}

CircuitBreakerStats AsyncMcpClient::circuit_stats() const {
    if (!circuit_breaker_) {
        return CircuitBreakerStats{};
    }
    return circuit_breaker_->stats();
}

void AsyncMcpClient::force_circuit_open() {
    if (circuit_breaker_) {
        circuit_breaker_->force_open();
    }
}

void AsyncMcpClient::force_circuit_closed() {
    if (circuit_breaker_) {
        circuit_breaker_->force_close();
    }
}

void AsyncMcpClient::on_circuit_state_change(CircuitBreaker::StateChangeCallback callback) {
    if (circuit_breaker_) {
        circuit_breaker_->on_state_change(std::move(callback));
    }
}

}  // namespace mcpp::async
