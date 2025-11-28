#include "mcpp/transport/http_transport.hpp"
#include "mcpp/json/fast_json.hpp"
#include "mcpp/log/logger.hpp"

#include <thread>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

HttpTransport::HttpTransport(HttpTransportConfig config)
    : HttpTransport(std::move(config), make_http_client())
{}

HttpTransport::HttpTransport(HttpTransportConfig config, std::unique_ptr<IHttpClient> client)
    : config_(std::move(config))
    , http_client_(std::move(client))
    , session_manager_(SessionManagerConfig{
          .max_reconnect_attempts = config_.max_retries,
          .reconnect_base_delay = std::chrono::milliseconds{100},
          .reconnect_max_delay = std::chrono::milliseconds{5000}
      })
{
    // Parse the base URL
    auto parsed = parse_url(config_.base_url);
    const bool url_valid = parsed.has_value();
    if (url_valid == false) {
        throw std::invalid_argument("Invalid base_url: " + config_.base_url);
    }
    url_ = std::move(*parsed);

    // Configure the HTTP client
    configure_client();

    // Initialize retry components
    init_retry_components();

    // Set up internal session manager logging
    session_manager_.on_state_change([](SessionState old_state, SessionState new_state) {
        get_logger().debug_fmt("Session state: {} -> {}", 
            to_string(old_state), to_string(new_state));
    });
}

void HttpTransport::init_retry_components() {
    // Use configured backoff policy or create default
    const bool has_backoff = (config_.backoff_policy != nullptr);
    if (has_backoff) {
        backoff_policy_ = config_.backoff_policy;
    } else {
        backoff_policy_ = std::make_shared<ExponentialBackoff>();
    }

    // Use configured retry policy or create default
    const bool has_retry_policy = (config_.retry_policy != nullptr);
    if (has_retry_policy) {
        retry_policy_ = config_.retry_policy;
    } else {
        retry_policy_ = std::make_shared<RetryPolicy>();
        retry_policy_->with_max_attempts(config_.max_retries);
    }
}

HttpTransport::~HttpTransport() {
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void HttpTransport::start() {
    const bool already_running = running_.exchange(true);
    if (already_running) {
        MCPP_LOG_WARN("HttpTransport::start() called but already running");
        return;  // Already started
    }

    MCPP_LOG_INFO("HttpTransport starting");
    get_logger().debug_fmt("Base URL: {}", config_.base_url);

    // Reset HTTP client to allow requests after previous stop/start cycle
    http_client_->reset();

    // Transition session state to Connecting
    session_manager_.begin_connect();

    // Optionally start SSE reader thread for server-initiated messages
    const bool should_open_sse = config_.auto_open_sse_stream;
    if (should_open_sse) {
        MCPP_LOG_DEBUG("Starting SSE reader thread");
        sse_thread_ = std::thread([this]() {
            sse_reader_loop();
        });
    }
}

void HttpTransport::stop() {
    const bool was_running = running_.exchange(false);
    if (was_running == false) {
        MCPP_LOG_TRACE("HttpTransport::stop() called but not running");
        return;  // Already stopped
    }

    MCPP_LOG_INFO("HttpTransport stopping");

    // Transition to Closing state
    session_manager_.begin_close();

    // Send DELETE to close session if we have one
    {
        auto session_to_close = session_manager_.session_id();
        const bool has_session = session_to_close.has_value();
        if (has_session) {
            MCPP_LOG_DEBUG("Closing session via DELETE request");
            // Build headers with session ID
            HeaderMap headers;
            headers["Accept"] = "application/json";
            headers["Mcp-Session-Id"] = *session_to_close;
            // Best effort - ignore errors
            (void)http_client_->del(url_.path_with_query(), headers);
        }
    }

    // Cancel pending HTTP requests
    MCPP_LOG_TRACE("Cancelling pending HTTP requests");
    http_client_->cancel();

    // Wake up any waiting receivers
    queue_cv_.notify_all();

    // Wait for SSE thread to finish
    if (sse_thread_.joinable()) {
        MCPP_LOG_TRACE("Waiting for SSE reader thread to finish");
        sse_thread_.join();
    }

    // Transition to Disconnected
    session_manager_.close_complete();

    MCPP_LOG_INFO("HttpTransport stopped");
}

bool HttpTransport::is_running() const {
    return running_.load();
}

// ─────────────────────────────────────────────────────────────────────────────
// Synchronous Operations
// ─────────────────────────────────────────────────────────────────────────────

HttpResult<void> HttpTransport::send(const Json& message) {
    const bool is_stopped = (running_.load() == false);
    if (is_stopped) {
        return tl::unexpected(HttpTransportError::closed());
    }

    return do_post_with_retry(message);
}

HttpResult<Json> HttpTransport::receive() {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    // Wait until we have a message or transport stops
    queue_cv_.wait(lock, [this]() {
        const bool has_message = (message_queue_.empty() == false);
        const bool is_stopped = (running_.load() == false);
        return has_message || is_stopped;
    });

    // Check if we stopped
    const bool is_stopped = (running_.load() == false);
    const bool queue_empty = message_queue_.empty();
    if (is_stopped && queue_empty) {
        return tl::unexpected(HttpTransportError::closed());
    }

    // Pop and return the message
    Json message = std::move(message_queue_.front());
    message_queue_.pop();
    return message;
}

HttpResult<std::optional<Json>> HttpTransport::receive_with_timeout(
    std::chrono::milliseconds timeout
) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    // Wait with timeout
    const bool got_message = queue_cv_.wait_for(lock, timeout, [this]() {
        const bool has_message = (message_queue_.empty() == false);
        const bool is_stopped = (running_.load() == false);
        return has_message || is_stopped;
    });

    // Timeout expired
    if (got_message == false) {
        return std::nullopt;
    }

    // Check if we stopped
    const bool is_stopped = (running_.load() == false);
    const bool queue_empty = message_queue_.empty();
    if (is_stopped && queue_empty) {
        return tl::unexpected(HttpTransportError::closed());
    }

    // Pop and return the message
    Json message = std::move(message_queue_.front());
    message_queue_.pop();
    return message;
}

// ─────────────────────────────────────────────────────────────────────────────
// Asynchronous Operations
// ─────────────────────────────────────────────────────────────────────────────

std::future<HttpResult<void>> HttpTransport::async_send(const Json& message) {
    return std::async(std::launch::async, [this, message]() {
        return this->send(message);
    });
}

std::future<HttpResult<Json>> HttpTransport::async_receive() {
    return std::async(std::launch::async, [this]() {
        return this->receive();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Session Info
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::string> HttpTransport::session_id() const {
    return session_manager_.session_id();
}

SessionState HttpTransport::session_state() const {
    return session_manager_.state();
}

const HttpTransportConfig& HttpTransport::config() const {
    return config_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Session Events
// ─────────────────────────────────────────────────────────────────────────────

void HttpTransport::on_session_state_change(SessionManager::StateChangeCallback callback) {
    session_manager_.on_state_change(std::move(callback));
}

void HttpTransport::on_session_established(SessionManager::SessionEstablishedCallback callback) {
    session_manager_.on_session_established(std::move(callback));
}

void HttpTransport::on_session_lost(SessionManager::SessionLostCallback callback) {
    session_manager_.on_session_lost(std::move(callback));
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

void HttpTransport::configure_client() {
    // Set base URL (scheme + host + port)
    std::string base = url_.scheme + "://" + url_.host_with_port();
    http_client_->set_base_url(base);

    // Set timeouts
    http_client_->set_connect_timeout(config_.connect_timeout);
    http_client_->set_read_timeout(config_.read_timeout);

    // Set SSL verification
    http_client_->set_verify_ssl(config_.tls.verify_peer);

    // Set default headers from config
    http_client_->set_default_headers(config_.default_headers);
}

HeaderMap HttpTransport::build_request_headers() {
    HeaderMap headers;
    headers["Accept"] = "application/json, text/event-stream";

    // Add session ID if we have one
    auto session = session_manager_.session_id();
    const bool has_session = session.has_value();
    if (has_session) {
        headers["Mcp-Session-Id"] = *session;
    }

    // Add Last-Event-ID for SSE resumption if available
    auto last_event = session_manager_.last_event_id();
    const bool has_last_event = last_event.has_value();
    if (has_last_event) {
        headers["Last-Event-ID"] = *last_event;
    }

    return headers;
}

bool HttpTransport::should_retry(const HttpTransportError& error, std::size_t attempt) {
    // Check if error type is retryable
    const bool type_retryable = retry_policy_->should_retry(error.code, attempt);
    if (type_retryable) {
        return true;
    }

    // For HTTP errors, check status code
    const bool is_http_error = (error.code == HttpTransportError::Code::HttpError);
    const bool has_status = error.http_status.has_value();
    if (is_http_error && has_status) {
        const bool within_limit = (attempt < retry_policy_->max_attempts());
        const bool status_retryable = retry_policy_->should_retry_http_status(*error.http_status);
        return within_limit && status_retryable;
    }

    return false;
}

std::chrono::milliseconds HttpTransport::get_retry_delay(
    std::size_t attempt,
    const HttpClientResponse* response
) {
    // Check for Retry-After header (case-insensitive per RFC 7230)
    if (response != nullptr) {
        const auto retry_after = get_header(response->headers, "Retry-After");
        const bool has_retry_after = retry_after.has_value();
        if (has_retry_after) {
            try {
                const int seconds = std::stoi(*retry_after);
                return std::chrono::milliseconds{seconds * 1000};
            } catch (...) {
                // Ignore invalid Retry-After values
            }
        }
    }

    // Use backoff policy
    return backoff_policy_->next_delay(attempt);
}

HttpResult<void> HttpTransport::do_post_with_retry(const Json& message) {
    HttpResult<void> last_result;
    const HttpClientResponse* last_response = nullptr;

    for (std::size_t attempt = 0; ; ++attempt) {
        // Check if still running
        const bool is_stopped = (running_.load() == false);
        if (is_stopped) {
            return tl::unexpected(HttpTransportError::closed());
        }

        // Try the request
        last_result = do_post(message);

        // Success - return immediately
        const bool succeeded = last_result.has_value();
        if (succeeded) {
            // Reset backoff on success
            backoff_policy_->reset();
            return last_result;
        }

        // Check if we should retry
        const auto& error = last_result.error();
        const bool should_retry_now = should_retry(error, attempt);

        if (should_retry_now == false) {
            get_logger().debug_fmt("Not retrying: attempt={}, error={}", attempt, error.message);
            return last_result;
        }

        // Calculate delay
        const auto delay = get_retry_delay(attempt, last_response);
        get_logger().info_fmt("Retrying in {}ms (attempt {}/{})", 
            delay.count(), attempt + 1, retry_policy_->max_attempts());

        // Wait before retry
        std::this_thread::sleep_for(delay);
    }

    // Should never reach here, but satisfy compiler
    return last_result;
}

HttpResult<void> HttpTransport::do_post(const Json& message) {
    // Serialize JSON
    const std::string body = message.dump();
    
    // Check body size limit
    if (config_.max_request_body_size > 0 && body.size() > config_.max_request_body_size) {
        return tl::unexpected(HttpTransportError{
            HttpTransportError::Code::InvalidResponse,
            "Request body too large: " + std::to_string(body.size()) + " bytes (max: " + 
            std::to_string(config_.max_request_body_size) + ")",
            std::nullopt
        });
    }
    
    MCPP_LOG_TRACE("POST request body prepared");

    // Build headers
    auto headers = build_request_headers();

    // Make the POST request
    auto result = http_client_->post(
        url_.path_with_query(),
        body,
        "application/json",
        headers
    );

    // Check for network errors
    const bool request_failed = (result.has_value() == false);
    if (request_failed) {
        get_logger().error_fmt("HTTP POST failed: {}", result.error().message);
        
        // Update session state on connection failure
        const auto current_state = session_manager_.state();
        const bool is_connecting = (current_state == SessionState::Connecting);
        const bool is_reconnecting = (current_state == SessionState::Reconnecting);
        if (is_connecting || is_reconnecting) {
            session_manager_.connection_failed(result.error().message);
        }
        
        return tl::unexpected(HttpTransportError::from_client_error(result.error()));
    }

    const auto& response = *result;
    get_logger().debug_fmt("HTTP POST response: {}", response.status_code);

    // 202 Accepted = notification sent successfully
    const bool is_accepted = (response.status_code == 202);
    if (is_accepted) {
        MCPP_LOG_TRACE("Request accepted (202)");
        return {};  // Success, no response body
    }

    // 404 handling depends on whether we have a session
    const bool is_not_found = (response.status_code == 404);
    if (is_not_found) {
        // Only treat as session expiration if we actually had a session
        const bool has_session = session_manager_.session_id().has_value();
        if (has_session) {
            MCPP_LOG_WARN("Session expired (404)");
            session_manager_.session_expired();
            return handle_session_expired(message);
        } else {
            // No session - this is just a 404 error (e.g., wrong endpoint)
            get_logger().error_fmt("HTTP 404: {}", response.body);
            return tl::unexpected(HttpTransportError::http_error(404, response.body));
        }
    }

    // 4xx/5xx = error
    const bool is_error = (response.status_code >= 400);
    if (is_error) {
        get_logger().error_fmt("HTTP error {}: {}", response.status_code, response.body);
        return tl::unexpected(HttpTransportError::http_error(response.status_code, response.body));
    }

    // Process response based on Content-Type
    if (response.is_sse()) {
        MCPP_LOG_DEBUG("Processing SSE response");
        process_sse_response(response);
    } else if (response.is_json()) {
        MCPP_LOG_DEBUG("Processing JSON response");
        // Use simdjson for fast parsing
        auto parse_result = fast_parse(response.body);
        const bool parse_ok = parse_result.has_value();
        if (parse_ok) {
            enqueue_message(std::move(*parse_result));
        } else {
            get_logger().error_fmt("JSON parse error: {}", parse_result.error().message);
            return tl::unexpected(HttpTransportError::parse_error(parse_result.error().message));
        }
    }

    // Extract session ID from response headers - this establishes the session
    // HTTP headers are case-insensitive, so we do a case-insensitive search
    // Note: Removed fragile length check - just compare lowercased names directly
    std::optional<std::string> session_header_value;
    for (const auto& [name, value] : response.headers) {
        MCPP_LOG_TRACE("Response header: " + name + " = " + value);
        // Case-insensitive comparison (no length check - that was fragile)
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        if (lower_name == "mcp-session-id") {
            session_header_value = value;
            break;
        }
    }
    
    if (session_header_value) {
        if (session_manager_.connection_established(*session_header_value)) {
            // Truncate session ID in logs for security (don't expose full token)
            const auto& sid = *session_header_value;
            std::string truncated = sid.size() > 16 
                ? sid.substr(0, 8) + "..." + sid.substr(sid.size() - 4)
                : sid;
            get_logger().info_fmt("Session established: {}", truncated);
        } else {
            // Sanitize invalid session ID - only show length and first 20 chars
            const auto& sid = *session_header_value;
            std::string preview = sid.substr(0, std::min(sid.size(), std::size_t{20}));
            // Remove any control characters from preview
            for (char& c : preview) {
                if (c < 32 || c > 126) c = '?';
            }
            get_logger().warn_fmt("Rejected invalid session ID from server (length={}, preview='{}')", 
                                   sid.size(), preview);
        }
    }

    return {};  // Success
}

HttpResult<void> HttpTransport::handle_session_expired(const Json& original_message) {
    // Session expired - check if we should retry
    const auto state = session_manager_.state();
    const bool is_reconnecting = (state == SessionState::Reconnecting);
    
    if (is_reconnecting == false) {
        // Not in reconnecting state, just return the error
        return tl::unexpected(HttpTransportError::session_expired());
    }

    MCPP_LOG_INFO("Attempting to re-establish session after expiration");

    // Retry the request without session ID (server will create new session)
    // Build headers without session ID
    HeaderMap headers;
    headers["Accept"] = "application/json, text/event-stream";

    const std::string body = original_message.dump();
    auto result = http_client_->post(
        url_.path_with_query(),
        body,
        "application/json",
        headers
    );

    const bool request_failed = (result.has_value() == false);
    if (request_failed) {
        session_manager_.connection_failed(result.error().message);
        return tl::unexpected(HttpTransportError::from_client_error(result.error()));
    }

    const auto& response = *result;

    // Check for another 404 (shouldn't happen, but handle it)
    const bool is_not_found = (response.status_code == 404);
    if (is_not_found) {
        session_manager_.connection_failed("Session re-establishment failed (404)");
        return tl::unexpected(HttpTransportError::session_expired());
    }

    // Check for other errors
    const bool is_error = (response.status_code >= 400);
    if (is_error) {
        session_manager_.connection_failed("HTTP error during reconnection");
        return tl::unexpected(HttpTransportError::http_error(response.status_code, response.body));
    }

    // Process response
    if (response.is_sse()) {
        process_sse_response(response);
    } else if (response.is_json()) {
        auto parse_result = fast_parse(response.body);
        if (parse_result.has_value()) {
            enqueue_message(std::move(*parse_result));
        }
    }

    // Extract new session ID - use case-insensitive lookup (HTTP headers are case-insensitive)
    std::optional<std::string> new_session_id;
    for (const auto& [name, value] : response.headers) {
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        if (lower_name == "mcp-session-id") {
            new_session_id = value;
            break;
        }
    }
    
    if (new_session_id) {
        if (session_manager_.connection_established(*new_session_id)) {
            session_manager_.clear_last_event_id();  // Clear old event ID after reconnection
            // Truncate session ID in logs for security
            const auto& sid = *new_session_id;
            std::string truncated = sid.size() > 16 
                ? sid.substr(0, 8) + "..." + sid.substr(sid.size() - 4)
                : sid;
            get_logger().info_fmt("Session re-established: {}", truncated);
        } else {
            get_logger().warn_fmt("Rejected invalid session ID from server (length={}, sanitized)",
                                   new_session_id->size());
        }
    }

    return {};  // Success
}

void HttpTransport::process_sse_response(const HttpClientResponse& response) {
    auto events = sse_parser_.feed(response.body);
    for (const auto& event : events) {
        process_sse_event(event);
    }
}

void HttpTransport::process_sse_event(const SseEvent& event) {
    // Track event ID for resumption (even if event has no data)
    const bool has_event_id = event.id.has_value();
    if (has_event_id) {
        session_manager_.record_event_id(*event.id);
    }

    // SSE events should have data
    const bool has_data = (event.data.empty() == false);
    if (has_data == false) {
        MCPP_LOG_TRACE("SSE event without data (keep-alive ping)");
        return;  // Ignore events without data (keep-alive pings)
    }

    // Parse JSON from data field using simdjson (SIMD-accelerated)
    auto parse_result = fast_parse(event.data);
    const bool parse_ok = parse_result.has_value();

    if (parse_ok) {
        MCPP_LOG_DEBUG("Received SSE message");
        enqueue_message(std::move(*parse_result));
    } else {
        // Log malformed JSON but don't fail - SSE streams can have noise
        get_logger().warn_fmt(
            "Malformed JSON in SSE event: {} (data: '{}')",
            parse_result.error().message,
            event.data.substr(0, 100)  // Truncate for logging
        );
    }
}

void HttpTransport::enqueue_message(Json message) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        message_queue_.push(std::move(message));
    }
    queue_cv_.notify_one();
}

void HttpTransport::sse_reader_loop() {
    // This loop maintains a persistent GET connection for server-initiated messages.
    MCPP_LOG_DEBUG("SSE reader loop started");

    while (running_.load()) {
        // Build headers for SSE request
        HeaderMap headers;
        headers["Accept"] = "text/event-stream";

        // Add session ID if we have one
        auto session = session_manager_.session_id();
        const bool has_session = session.has_value();
        if (has_session) {
            headers["Mcp-Session-Id"] = *session;
        }

        // Add Last-Event-ID for resumption
        auto last_event = session_manager_.last_event_id();
        const bool has_last_event = last_event.has_value();
        if (has_last_event) {
            headers["Last-Event-ID"] = *last_event;
            get_logger().debug_fmt("SSE resuming from event ID: {}", *last_event);
        }

        // Make GET request
        MCPP_LOG_TRACE("SSE GET request starting");
        auto result = http_client_->get(url_.path_with_query(), headers);

        // Process response if successful
        const bool request_ok = result.has_value() && result->status_code == 200;
        if (request_ok) {
            const auto& response = *result;
            if (response.is_sse()) {
                MCPP_LOG_TRACE("Processing SSE stream data");
                process_sse_response(response);
            }
        } else if (result.has_value() == false) {
            get_logger().warn_fmt("SSE connection error: {}", result.error().message);
        } else if (result->status_code == 404) {
            // Session expired on SSE stream
            MCPP_LOG_WARN("SSE stream session expired (404)");
            session_manager_.session_expired();
        } else {
            get_logger().warn_fmt("SSE request failed with status: {}", result->status_code);
        }

        // Configurable delay before retry (avoid tight loop on errors)
        const bool still_running = running_.load();
        if (still_running) {
            MCPP_LOG_TRACE("SSE reader sleeping before reconnect");
            std::this_thread::sleep_for(config_.sse_reconnect_delay);
        }
    }

    MCPP_LOG_DEBUG("SSE reader loop exiting");
}

}  // namespace mcpp
