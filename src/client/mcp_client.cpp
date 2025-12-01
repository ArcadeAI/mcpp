#include "mcpp/client/mcp_client.hpp"
#include "mcpp/client/handler_utils.hpp"
#include "mcpp/log/logger.hpp"
#include "mcpp/security/url_validator.hpp"

#include <chrono>
#include <future>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

McpClient::McpClient(McpClientConfig config)
    : config_(std::move(config))
    , transport_(std::make_unique<HttpTransport>(config_.transport))
{
    if (config_.enable_circuit_breaker) {
        circuit_breaker_ = std::make_unique<CircuitBreaker>(config_.circuit_breaker);
    }
}

McpClient::McpClient(McpClientConfig config, std::unique_ptr<IHttpClient> http_client)
    : config_(std::move(config))
    , transport_(std::make_unique<HttpTransport>(config_.transport, std::move(http_client)))
{
    if (config_.enable_circuit_breaker) {
        circuit_breaker_ = std::make_unique<CircuitBreaker>(config_.circuit_breaker);
    }
}

McpClient::~McpClient() {
    disconnect();
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

McpResult<InitializeResult> McpClient::connect() {
    if (connected_) {
        return tl::unexpected(McpClientError::protocol_error("Already connected"));
    }

    auto start_result = transport_->start();
    if (!start_result) {
        return tl::unexpected(McpClientError::transport_error(start_result.error().message));
    }
    connected_ = true;

    if (config_.auto_initialize) {
        // Build initialize params
        InitializeParams params;
        params.protocol_version = MCP_PROTOCOL_VERSION;
        params.client_info = {config_.client_name, config_.client_version};
        params.capabilities = config_.capabilities;

        // Send initialize request
        auto result = send_request("initialize", params.to_json());
        if (!result) {
            disconnect();
            return tl::unexpected(result.error());
        }

        // Parse response
        auto init_result = InitializeResult::from_json(*result);
        server_info_ = init_result.server_info;
        server_capabilities_ = init_result.capabilities;
        server_instructions_ = init_result.instructions;

        // Send initialized notification
        auto notify_result = send_notification("notifications/initialized");
        if (!notify_result) {
            disconnect();
            return tl::unexpected(notify_result.error());
        }

        initialized_ = true;
        MCPP_LOG_INFO("MCP client initialized");

        return init_result;
    }

    return InitializeResult{};  // Empty result when not auto-initializing
}

void McpClient::disconnect() {
    if (!connected_) {
        return;
    }

    transport_->stop();
    connected_ = false;
    initialized_ = false;
    server_info_.reset();
    server_capabilities_.reset();
    server_instructions_.reset();

    MCPP_LOG_INFO("MCP client disconnected");
}

bool McpClient::is_connected() const {
    return connected_;
}

bool McpClient::is_initialized() const {
    return initialized_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Server Information
// ─────────────────────────────────────────────────────────────────────────────

std::optional<Implementation> McpClient::server_info() const {
    return server_info_;
}

std::optional<ServerCapabilities> McpClient::server_capabilities() const {
    return server_capabilities_;
}

std::optional<std::string> McpClient::server_instructions() const {
    return server_instructions_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tools API
// ─────────────────────────────────────────────────────────────────────────────

McpResult<ListToolsResult> McpClient::list_tools(std::optional<std::string> cursor) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = send_request("tools/list", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return ListToolsResult::from_json(*result);
}

McpResult<CallToolResult> McpClient::call_tool(
    const std::string& name,
    const Json& arguments,
    std::optional<ProgressToken> progress_token
) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    CallToolParams params;
    params.name = name;
    params.arguments = arguments;
    
    if (progress_token) {
        params.meta = RequestMeta{.progress_token = std::move(progress_token)};
    }

    auto result = send_request("tools/call", params.to_json());
    if (!result) {
        return tl::unexpected(result.error());
    }

    return CallToolResult::from_json(*result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resources API
// ─────────────────────────────────────────────────────────────────────────────

McpResult<ListResourcesResult> McpClient::list_resources(std::optional<std::string> cursor) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = send_request("resources/list", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return ListResourcesResult::from_json(*result);
}

McpResult<ReadResourceResult> McpClient::read_resource(
    const std::string& uri,
    std::optional<ProgressToken> progress_token
) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = {{"uri", uri}};
    
    if (progress_token) {
        RequestMeta meta{.progress_token = std::move(progress_token)};
        params["_meta"] = meta.to_json();
    }

    auto result = send_request("resources/read", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return ReadResourceResult::from_json(*result);
}

McpResult<void> McpClient::subscribe_resource(const std::string& uri) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }
    
    // Check if server supports subscriptions
    if (server_capabilities_ && server_capabilities_->resources) {
        if (!server_capabilities_->resources->subscribe) {
            return tl::unexpected(McpClientError{
                ClientErrorCode::ProtocolError,
                "Server does not support resource subscriptions",
                std::nullopt
            });
        }
    }

    Json params = {{"uri", uri}};

    auto result = send_request("resources/subscribe", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return {};
}

McpResult<void> McpClient::unsubscribe_resource(const std::string& uri) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = {{"uri", uri}};

    auto result = send_request("resources/unsubscribe", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return {};
}

McpResult<ListResourceTemplatesResult> McpClient::list_resource_templates(
    std::optional<std::string> cursor
) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = send_request("resources/templates/list", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return ListResourceTemplatesResult::from_json(*result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Prompts API
// ─────────────────────────────────────────────────────────────────────────────

McpResult<ListPromptsResult> McpClient::list_prompts(std::optional<std::string> cursor) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = Json::object();
    if (cursor) {
        params["cursor"] = *cursor;
    }

    auto result = send_request("prompts/list", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return ListPromptsResult::from_json(*result);
}

McpResult<GetPromptResult> McpClient::get_prompt(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& arguments,
    std::optional<ProgressToken> progress_token
) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = {{"name", name}};
    if (!arguments.empty()) {
        params["arguments"] = arguments;
    }
    
    if (progress_token) {
        RequestMeta meta{.progress_token = std::move(progress_token)};
        params["_meta"] = meta.to_json();
    }

    auto result = send_request("prompts/get", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return GetPromptResult::from_json(*result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Completion API
// ─────────────────────────────────────────────────────────────────────────────

McpResult<CompleteResult> McpClient::complete_prompt(
    const std::string& prompt_name,
    const std::string& argument_name,
    const std::string& argument_value
) {
    CompleteParams params;
    params.ref = CompletionReference{CompletionRefType::Prompt, prompt_name};
    params.argument = CompletionArgument{argument_name, argument_value};
    return complete(params);
}

McpResult<CompleteResult> McpClient::complete_resource(
    const std::string& resource_uri,
    const std::string& argument_name,
    const std::string& argument_value
) {
    CompleteParams params;
    params.ref = CompletionReference{CompletionRefType::Resource, resource_uri};
    params.argument = CompletionArgument{argument_name, argument_value};
    return complete(params);
}

McpResult<CompleteResult> McpClient::complete(const CompleteParams& params) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    auto result = send_request("completion/complete", params.to_json());
    if (!result) {
        return tl::unexpected(result.error());
    }

    return CompleteResult::from_json(*result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Logging API
// ─────────────────────────────────────────────────────────────────────────────

McpResult<void> McpClient::set_logging_level(LoggingLevel level) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }

    Json params = {{"level", to_string(level)}};

    auto result = send_request("logging/setLevel", params);
    if (!result) {
        return tl::unexpected(result.error());
    }

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Methods
// ─────────────────────────────────────────────────────────────────────────────

McpResult<void> McpClient::ping() {
    if (!connected_) {
        return tl::unexpected(McpClientError::not_connected());
    }

    auto result = send_request("ping");
    if (!result) {
        return tl::unexpected(result.error());
    }

    return {};
}

McpResult<void> McpClient::cancel_request(
    std::variant<std::string, int> request_id,
    std::optional<std::string> reason
) {
    if (!connected_) {
        return tl::unexpected(McpClientError::not_connected());
    }

    CancelledNotification notification;
    notification.request_id = std::move(request_id);
    notification.reason = std::move(reason);

    return send_notification("notifications/cancelled", notification.to_json());
}

// ─────────────────────────────────────────────────────────────────────────────
// Event Handlers
// ─────────────────────────────────────────────────────────────────────────────

void McpClient::on_notification(NotificationHandler handler) {
    notification_handler_ = std::move(handler);
}

void McpClient::on_tool_list_changed(ToolListChangedCallback callback) {
    tool_list_changed_handler_ = std::move(callback);
}

void McpClient::on_resource_list_changed(ResourceListChangedCallback callback) {
    resource_list_changed_handler_ = std::move(callback);
}

void McpClient::on_resource_updated(std::function<void(const std::string&)> callback) {
    resource_updated_handler_ = std::move(callback);
}

void McpClient::on_prompt_list_changed(PromptListChangedCallback callback) {
    prompt_list_changed_handler_ = std::move(callback);
}

void McpClient::on_log_message(LogMessageCallback callback) {
    log_message_handler_ = std::move(callback);
}

void McpClient::on_progress(ProgressCallback callback) {
    progress_handler_ = std::move(callback);
}

// ─────────────────────────────────────────────────────────────────────────────
// Client Capability Handlers
// ─────────────────────────────────────────────────────────────────────────────

void McpClient::set_elicitation_handler(std::shared_ptr<IElicitationHandler> handler) {
    elicitation_handler_ = std::move(handler);
}

McpResult<Json> McpClient::handle_elicitation_request(const Json& params) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
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
            return ElicitationResult{ElicitationAction::Decline, std::nullopt}.to_json();
        }
        if (validation.warning) {
            MCPP_LOG_WARN("Elicitation URL warning: " + *validation.warning);
        }
        
        if (elicitation_handler_) {
            result = elicitation_handler_->handle_url(
                url_params.elicitation_id,
                url_params.url,
                url_params.message
            );
        } else {
            result = {ElicitationAction::Dismiss, std::nullopt};
        }
    } else {
        // Form mode elicitation (default) - no URL validation needed
        auto form_params = FormElicitationParams::from_json(params);
        
        if (elicitation_handler_) {
            result = elicitation_handler_->handle_form(
                form_params.message,
                form_params.requested_schema
            );
        } else {
            result = {ElicitationAction::Dismiss, std::nullopt};
        }
    }
    
    return result.to_json();
}

void McpClient::set_sampling_handler(std::shared_ptr<ISamplingHandler> handler) {
    sampling_handler_ = std::move(handler);
}

McpResult<Json> McpClient::handle_sampling_request(const Json& params) {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }
    
    if (!sampling_handler_) {
        return tl::unexpected(McpClientError::protocol_error("No sampling handler configured"));
    }
    
    // Parse the request
    auto create_params = CreateMessageParams::from_json(params);
    
    // Call the handler
    auto result = sampling_handler_->handle_create_message(create_params);
    
    if (!result) {
        // Handler declined
        return tl::unexpected(McpClientError::protocol_error("Sampling request declined by handler"));
    }
    
    return result->to_json();
}

void McpClient::set_roots_handler(std::shared_ptr<IRootsHandler> handler) {
    roots_handler_ = std::move(handler);
}

McpResult<Json> McpClient::handle_roots_list_request() {
    if (!initialized_) {
        return tl::unexpected(McpClientError::not_initialized());
    }
    
    ListRootsResult result;
    
    if (roots_handler_) {
        result = roots_handler_->list_roots();
    }
    // If no handler, return empty list (valid response per spec)
    
    return result.to_json();
}

McpResult<void> McpClient::notify_roots_changed() {
    if (!connected_) {
        return tl::unexpected(McpClientError::not_connected());
    }
    return send_notification("notifications/roots/list_changed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Low-Level Access
// ─────────────────────────────────────────────────────────────────────────────

McpResult<Json> McpClient::send_request(const std::string& method, const Json& params) {
    if (!connected_) {
        return tl::unexpected(McpClientError::not_connected());
    }

    Json request = {
        {"jsonrpc", "2.0"},
        {"id", next_request_id()},
        {"method", method}
    };

    if (!params.empty()) {
        request["params"] = params;
    }

    return send_and_receive(request);
}

McpResult<void> McpClient::send_notification(const std::string& method, const Json& params) {
    if (!connected_) {
        return tl::unexpected(McpClientError::not_connected());
    }

    Json notification = {
        {"jsonrpc", "2.0"},
        {"method", method}
    };

    if (!params.empty()) {
        notification["params"] = params;
    }

    auto result = transport_->send(notification);
    if (!result) {
        return tl::unexpected(McpClientError::transport_error(result.error().message));
    }

    return {};
}

HttpTransport& McpClient::transport() {
    return *transport_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

McpResult<Json> McpClient::send_and_receive(const Json& request) {
    // Check circuit breaker before making request
    if (circuit_breaker_ && !circuit_breaker_->allow_request()) {
        return tl::unexpected(McpClientError{
            ClientErrorCode::TransportError,
            "Circuit breaker is open - server appears unhealthy",
            std::nullopt
        });
    }
    
    // Send the request
    auto send_result = transport_->send(request);
    if (!send_result) {
        if (circuit_breaker_) {
            circuit_breaker_->record_failure();
        }
        return tl::unexpected(McpClientError::transport_error(send_result.error().message));
    }

    // Calculate deadline for timeout
    const bool has_timeout = config_.request_timeout.count() > 0;
    const auto deadline = has_timeout 
        ? std::chrono::steady_clock::now() + config_.request_timeout 
        : std::chrono::steady_clock::time_point::max();

    // Receive messages until we get our response
    // Server may send requests/notifications while we wait
    while (true) {
        // Check timeout
        if (has_timeout && std::chrono::steady_clock::now() >= deadline) {
            if (circuit_breaker_) {
                circuit_breaker_->record_failure();
            }
            return tl::unexpected(McpClientError{
                ClientErrorCode::TransportError,
                "Request timeout after " + std::to_string(config_.request_timeout.count()) + "ms",
                std::nullopt
            });
        }
        
        // Calculate remaining time for this receive
        auto remaining = has_timeout 
            ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
            : std::chrono::milliseconds{1000};  // Default poll interval when no timeout
        
        if (remaining.count() <= 0) {
            remaining = std::chrono::milliseconds{1};  // Minimum poll
        }
        
        // Use receive_with_timeout to avoid blocking forever
        auto recv_result = transport_->receive_with_timeout(remaining);
        if (!recv_result) {
            if (circuit_breaker_) {
                circuit_breaker_->record_failure();
            }
            return tl::unexpected(McpClientError::transport_error(recv_result.error().message));
        }
        
        // Check if we got a message or timed out
        if (!recv_result->has_value()) {
            // No message received, loop to check deadline
            continue;
        }

        const Json& message = recv_result->value();
        
        // Check message type
        bool has_id = message.contains("id") && !message["id"].is_null();
        bool has_method = message.contains("method");

        if (has_method && has_id) {
            // Server request - handle and respond, then continue waiting
            handle_server_request(message);
        }
        else if (has_method) {
            // Notification - dispatch and continue waiting
            dispatch_notification(message);
        }
        else if (has_id) {
            // Response to our request
            auto result = extract_result(message);
            
            // Record success/failure based on response content
            if (circuit_breaker_) {
                if (result.has_value()) {
                    circuit_breaker_->record_success();
                }
                // Note: Application-level errors (from server) don't trip the circuit
                // Only transport/timeout errors should trip it
            }
            return result;
        }
        // else: malformed message, ignore and continue
    }
}

McpResult<Json> McpClient::extract_result(const Json& response) {
    // Check for error response
    if (response.contains("error")) {
        auto error = McpError::from_json(response["error"]);
        return tl::unexpected(McpClientError::from_rpc_error(error));
    }

    // Extract result
    if (response.contains("result")) {
        return response["result"];
    }

    // No result and no error - protocol error
    return tl::unexpected(McpClientError::protocol_error("Response missing 'result' field"));
}

void McpClient::handle_server_request(const Json& request) {
    std::string method = request["method"].get<std::string>();
    Json params = request.value("params", Json::object());
    auto request_id = request["id"];
    
    // Route to appropriate handler with timeout
    McpResult<Json> result;
    
    // Lambda to run handler
    auto run_handler = [this, &method, &params]() -> McpResult<Json> {
        if (method == "elicitation/create") {
            return handle_elicitation_request(params);
        }
        else if (method == "sampling/createMessage") {
            return handle_sampling_request(params);
        }
        else if (method == "roots/list") {
            return handle_roots_list_request();
        }
        else {
            return tl::unexpected(McpClientError::protocol_error("Method not found: " + method));
        }
    };
    
    // Apply timeout if configured
    const bool has_timeout = config_.handler_timeout.count() > 0;
    if (has_timeout) {
        // Run handler in async task with timeout
        auto future = std::async(std::launch::async, run_handler);
        auto status = future.wait_for(config_.handler_timeout);
        
        if (status == std::future_status::timeout) {
            MCPP_LOG_WARN("Handler timeout for method: " + method);
            result = tl::unexpected(McpClientError::timeout(
                "Handler timeout after " + std::to_string(config_.handler_timeout.count()) + "ms"
            ));
        } else {
            result = future.get();
        }
    } else {
        // No timeout - run synchronously
        result = run_handler();
    }
    
    // Send response
    send_response(request_id, result);
}

void McpClient::send_response(const Json& request_id, const McpResult<Json>& result) {
    Json response = {
        {"jsonrpc", "2.0"},
        {"id", request_id}
    };
    
    if (result.has_value()) {
        response["result"] = *result;
    } else {
        response["error"] = {
            {"code", static_cast<int>(result.error().code)},
            {"message", result.error().message}
        };
    }
    
    auto send_result = transport_->send(response);
    if (!send_result) {
        MCPP_LOG_WARN("Failed to send response: " + send_result.error().message);
    }
}

void McpClient::dispatch_notification(const Json& message) {
    std::string method = message["method"].get<std::string>();
    Json params = message.value("params", Json::object());
    
    // Call generic handler first
    // Safely invoke notification handler with exception protection
    try {
        if (notification_handler_) {
            notification_handler_(method, params);
        }
    } catch (const std::exception& e) {
        MCPP_LOG_ERROR("Exception in generic notification handler: " + std::string(e.what()));
    } catch (...) {
        MCPP_LOG_ERROR("Unknown exception in generic notification handler");
    }
    
    // Dispatch table for O(1) lookup instead of linear if-else chain
    // Each handler is wrapped in try-catch to ensure one failure doesn't affect others
    using Handler = std::function<void(McpClient*, const Json&)>;
    static const std::unordered_map<std::string_view, Handler> dispatch_table = {
        {"notifications/tools/list_changed", [](McpClient* self, const Json&) {
            if (self->tool_list_changed_handler_) {
                self->tool_list_changed_handler_();
            }
        }},
        {"notifications/resources/list_changed", [](McpClient* self, const Json&) {
            if (self->resource_list_changed_handler_) {
                self->resource_list_changed_handler_();
            }
        }},
        {"notifications/resources/updated", [](McpClient* self, const Json& params) {
            if (self->resource_updated_handler_) {
                auto notification = ResourceUpdatedNotification::from_json(params);
                self->resource_updated_handler_(notification.uri);
            }
        }},
        {"notifications/prompts/list_changed", [](McpClient* self, const Json&) {
            if (self->prompt_list_changed_handler_) {
                self->prompt_list_changed_handler_();
            }
        }},
        {"notifications/message", [](McpClient* self, const Json& params) {
            if (self->log_message_handler_) {
                std::string level_str = params.value("level", "info");
                LoggingLevel level = logging_level_from_string(level_str);
                auto logger_name = params.value("logger", "");
                auto data = params.value("data", "");
                self->log_message_handler_(level, logger_name, data);
            }
        }},
        {"notifications/progress", [](McpClient* self, const Json& params) {
            if (self->progress_handler_) {
                auto prog = ProgressNotification::from_json(params);
                self->progress_handler_(prog);
            }
        }}
    };

    if (auto it = dispatch_table.find(method); it != dispatch_table.end()) {
        try {
            it->second(this, params);
        } catch (const std::exception& e) {
            MCPP_LOG_ERROR("Exception in notification handler for '" + method + "': " + std::string(e.what()));
        } catch (...) {
            MCPP_LOG_ERROR("Unknown exception in notification handler for '" + method + "'");
        }
    }
}

uint64_t McpClient::next_request_id() {
    return ++request_id_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Circuit Breaker
// ─────────────────────────────────────────────────────────────────────────────

CircuitState McpClient::circuit_state() const {
    if (!circuit_breaker_) {
        return CircuitState::Closed;  // No breaker = always closed
    }
    return circuit_breaker_->state();
}

bool McpClient::is_circuit_open() const {
    if (!circuit_breaker_) {
        return false;
    }
    return circuit_breaker_->is_open();
}

CircuitBreakerStats McpClient::circuit_stats() const {
    if (!circuit_breaker_) {
        return CircuitBreakerStats{};
    }
    return circuit_breaker_->stats();
}

void McpClient::force_circuit_open() {
    if (circuit_breaker_) {
        circuit_breaker_->force_open();
    }
}

void McpClient::force_circuit_closed() {
    if (circuit_breaker_) {
        circuit_breaker_->force_close();
    }
}

void McpClient::on_circuit_state_change(CircuitBreaker::StateChangeCallback callback) {
    if (circuit_breaker_) {
        circuit_breaker_->on_state_change(std::move(callback));
    }
}

}  // namespace mcpp

