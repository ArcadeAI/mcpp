// ─────────────────────────────────────────────────────────────────────────────
// Handler Integration Tests
// ─────────────────────────────────────────────────────────────────────────────
// End-to-end tests for server → client handler request/response cycle.
// Tests: Elicitation, Sampling, Roots handlers with full message flow.

#include <catch2/catch_test_macros.hpp>

#include "mcpp/async/async_mcp_client.hpp"
#include "mcpp/async/async_transport.hpp"
#include "mcpp/client/elicitation_handler.hpp"
#include "mcpp/client/sampling_handler.hpp"
#include "mcpp/client/roots_handler.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

#include <atomic>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace mcpp;
using namespace mcpp::async;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Simple Mock Transport for Handler Testing
// ═══════════════════════════════════════════════════════════════════════════
// This mock simulates the transport layer. When the server sends a request,
// we queue it for the client's async_receive. When the client sends a response,
// we capture it for verification.

class HandlerTestTransport : public IAsyncTransport {
public:
    explicit HandlerTestTransport(asio::any_io_executor executor)
        : executor_(std::move(executor))
        , running_(false)
    {}

    asio::any_io_executor get_executor() override {
        return executor_;
    }

    asio::awaitable<TransportResult<void>> async_start() override {
        running_ = true;
        co_return TransportResult<void>{};
    }

    asio::awaitable<void> async_stop() override {
        running_ = false;
        co_return;
    }

    asio::awaitable<TransportResult<void>> async_send(Json message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sent_messages_.push(std::move(message));
        co_return TransportResult<void>{};
    }

    asio::awaitable<TransportResult<Json>> async_receive() override {
        // Check for queued messages
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!incoming_messages_.empty()) {
                auto msg = std::move(incoming_messages_.front());
                incoming_messages_.pop();
                co_return msg;
            }
        }
        
        // No message - return error (caller should retry)
        co_return tl::unexpected(TransportError{
            TransportError::Category::Timeout,
            "No message available",
            std::nullopt
        });
    }

    bool is_running() const override {
        return running_;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test Interface
    // ─────────────────────────────────────────────────────────────────────────

    // Queue a message to be received by the client
    void queue_incoming(Json message) {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_messages_.push(std::move(message));
    }

    // Get next message sent by client
    std::optional<Json> pop_sent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sent_messages_.empty()) {
            return std::nullopt;
        }
        auto msg = std::move(sent_messages_.front());
        sent_messages_.pop();
        return msg;
    }

    // Check if client sent any messages
    bool has_sent_messages() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !sent_messages_.empty();
    }

    std::size_t sent_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return sent_messages_.size();
    }

private:
    asio::any_io_executor executor_;
    std::atomic<bool> running_;
    
    mutable std::mutex mutex_;
    std::queue<Json> incoming_messages_;  // Server -> Client
    std::queue<Json> sent_messages_;      // Client -> Server
};

// ═══════════════════════════════════════════════════════════════════════════
// Recording Handlers (for verification)
// ═══════════════════════════════════════════════════════════════════════════

class RecordingElicitationHandler : public IElicitationHandler {
public:
    void set_response(ElicitationResult response) {
        response_ = std::move(response);
    }

    ElicitationResult handle_form(const std::string& message, const Json& schema) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++call_count_;
        last_message_ = message;
        last_schema_ = schema;
        return response_;
    }

    ElicitationResult handle_url(const std::string& id, const std::string& url, const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++url_call_count_;
        last_elicitation_id_ = id;
        last_url_ = url;
        return response_;
    }

    int call_count() const { return call_count_; }
    int url_call_count() const { return url_call_count_; }
    std::string last_message() const { return last_message_; }
    Json last_schema() const { return last_schema_; }
    std::string last_url() const { return last_url_; }

private:
    std::mutex mutex_;
    int call_count_ = 0;
    int url_call_count_ = 0;
    std::string last_message_;
    Json last_schema_;
    std::string last_elicitation_id_;
    std::string last_url_;
    ElicitationResult response_{ElicitationAction::Dismiss, std::nullopt};
};

class RecordingSamplingHandler : public ISamplingHandler {
public:
    void set_response(CreateMessageResult response) {
        response_ = std::move(response);
    }

    std::optional<CreateMessageResult> handle_create_message(const CreateMessageParams& params) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++call_count_;
        last_params_ = params;
        return response_;
    }

    int call_count() const { return call_count_; }
    const CreateMessageParams& last_params() const { return last_params_; }

private:
    std::mutex mutex_;
    int call_count_ = 0;
    CreateMessageParams last_params_;
    std::optional<CreateMessageResult> response_;
};

class RecordingRootsHandler : public IRootsHandler {
public:
    void set_roots(std::vector<Root> roots) {
        roots_ = std::move(roots);
    }

    ListRootsResult list_roots() override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++call_count_;
        return ListRootsResult{roots_};
    }

    int call_count() const { return call_count_; }

private:
    std::mutex mutex_;
    int call_count_ = 0;
    std::vector<Root> roots_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Helper to run coroutines synchronously
// ═══════════════════════════════════════════════════════════════════════════

template <typename T>
T run_sync(asio::io_context& io, asio::awaitable<T> coro) {
    std::promise<T> promise;
    auto future = promise.get_future();

    asio::co_spawn(io, [&]() -> asio::awaitable<T> {
        try {
            T result = co_await std::move(coro);
            promise.set_value(std::move(result));
            co_return result;
        } catch (...) {
            promise.set_exception(std::current_exception());
            throw;
        }
    }, asio::detached);

    io.run();
    io.restart();

    return future.get();
}

// ═══════════════════════════════════════════════════════════════════════════
// Test Setup Helper
// ═══════════════════════════════════════════════════════════════════════════

struct TestSetup {
    asio::io_context io;
    HandlerTestTransport* transport_ptr;  // Raw pointer for test access
    std::unique_ptr<AsyncMcpClient> client;

    TestSetup() {
        auto transport = std::make_unique<HandlerTestTransport>(io.get_executor());
        transport_ptr = transport.get();
        
        AsyncMcpClientConfig config;
        config.client_name = "test-client";
        config.client_version = "1.0.0";
        config.auto_initialize = false;
        
        client = std::make_unique<AsyncMcpClient>(std::move(transport), std::move(config));
    }

    void connect_client() {
        // Just connect without auto-initialize
        auto result = run_sync(io, client->connect());
        REQUIRE(result.has_value());
        REQUIRE(client->is_connected());
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Server elicitation/create triggers client handler", "[integration][handlers][elicitation]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingElicitationHandler>();
    handler->set_response({ElicitationAction::Accept, Json{{"name", "Alice"}}});
    setup.client->set_elicitation_handler(handler);
    
    setup.connect_client();
    
    // Directly call the handler method (simulating what dispatcher would do)
    Json params = {
        {"mode", "form"},
        {"message", "Please enter your name"},
        {"requestedSchema", {{"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}}}
    };
    
    auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
    
    // Verify handler was called
    REQUIRE(handler->call_count() == 1);
    REQUIRE(handler->last_message() == "Please enter your name");
    
    // Verify result
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "accept");
    REQUIRE((*result)["content"]["name"] == "Alice");
}

TEST_CASE("Elicitation dismiss action returns correct response", "[integration][handlers][elicitation]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingElicitationHandler>();
    handler->set_response({ElicitationAction::Dismiss, std::nullopt});
    setup.client->set_elicitation_handler(handler);
    
    setup.connect_client();
    
    Json params = {{"mode", "form"}, {"message", "Cancel this"}};
    auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "dismiss");
}

TEST_CASE("Elicitation decline action returns correct response", "[integration][handlers][elicitation]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingElicitationHandler>();
    handler->set_response({ElicitationAction::Decline, std::nullopt});
    setup.client->set_elicitation_handler(handler);
    
    setup.connect_client();
    
    Json params = {{"mode", "form"}, {"message", "Decline this"}};
    auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
}

// ═══════════════════════════════════════════════════════════════════════════
// Sampling Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Server sampling/createMessage triggers client handler", "[integration][handlers][sampling]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingSamplingHandler>();
    CreateMessageResult llm_response;
    llm_response.role = SamplingRole::Assistant;
    llm_response.content = TextContent{"Hello! How can I help?", std::nullopt};
    llm_response.model = "gpt-4";
    llm_response.stop_reason = StopReason::EndTurn;
    handler->set_response(llm_response);
    
    setup.client->set_sampling_handler(handler);
    setup.connect_client();
    
    Json params = {
        {"messages", Json::array({
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hi"}}}}
        })},
        {"maxTokens", 100}
    };
    
    auto result = run_sync(setup.io, setup.client->handle_sampling_request(params));
    
    REQUIRE(handler->call_count() == 1);
    REQUIRE(result.has_value());
    REQUIRE((*result)["role"] == "assistant");
    REQUIRE((*result)["model"] == "gpt-4");
}

TEST_CASE("Sampling handler returning nullopt sends error response", "[integration][handlers][sampling]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingSamplingHandler>();
    // Don't set a response - handler will return nullopt
    
    setup.client->set_sampling_handler(handler);
    setup.connect_client();
    
    Json params = {{"messages", Json::array()}, {"maxTokens", 100}};
    auto result = run_sync(setup.io, setup.client->handle_sampling_request(params));
    
    REQUIRE_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Roots Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Server roots/list triggers client handler", "[integration][handlers][roots]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingRootsHandler>();
    handler->set_roots({
        Root{"file:///project", "Project"},
        Root{"file:///shared", "Shared Files"}
    });
    
    setup.client->set_roots_handler(handler);
    setup.connect_client();
    
    auto result = run_sync(setup.io, setup.client->handle_roots_list_request());
    
    REQUIRE(handler->call_count() == 1);
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].size() == 2);
    REQUIRE((*result)["roots"][0]["uri"] == "file:///project");
    REQUIRE((*result)["roots"][0]["name"] == "Project");
}

TEST_CASE("Roots handler with empty list returns empty array", "[integration][handlers][roots]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingRootsHandler>();
    handler->set_roots({});  // Empty
    
    setup.client->set_roots_handler(handler);
    setup.connect_client();
    
    auto result = run_sync(setup.io, setup.client->handle_roots_list_request());
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Elicitation request without handler returns default dismiss", "[integration][handlers][error]") {
    TestSetup setup;
    // No handlers set
    
    setup.connect_client();
    
    Json params = {{"mode", "form"}, {"message", "Test"}};
    auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
    
    // Without handler, returns dismiss action
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "dismiss");
}

TEST_CASE("Sampling request without handler returns error", "[integration][handlers][error]") {
    TestSetup setup;
    // No sampling handler set
    
    setup.connect_client();
    
    Json params = {{"messages", Json::array()}, {"maxTokens", 100}};
    auto result = run_sync(setup.io, setup.client->handle_sampling_request(params));
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().message.find("handler") != std::string::npos);
}

// NOTE: Notification tests require more sophisticated async coordination
// and are better tested with real integration tests using actual MCP servers.
// See real_server_test.cpp for integration patterns.

// ═══════════════════════════════════════════════════════════════════════════
// Concurrent Request Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Multiple handler calls work correctly", "[integration][handlers][concurrency]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingElicitationHandler>();
    handler->set_response({ElicitationAction::Accept, Json{{"count", 1}}});
    setup.client->set_elicitation_handler(handler);
    
    setup.connect_client();
    
    // Call handler multiple times
    for (int i = 0; i < 5; ++i) {
        Json params = {{"mode", "form"}, {"message", "Request " + std::to_string(i)}};
        auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
        REQUIRE(result.has_value());
    }
    
    REQUIRE(handler->call_count() == 5);
}

// ═══════════════════════════════════════════════════════════════════════════
// URL Validation Security Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("URL elicitation rejects localhost URLs", "[integration][handlers][security]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingElicitationHandler>();
    handler->set_response({ElicitationAction::Opened, std::nullopt});
    setup.client->set_elicitation_handler(handler);
    
    setup.connect_client();
    
    // Try localhost URL - should be rejected without calling handler
    Json params = {
        {"mode", "url"},
        {"elicitationId", "test-123"},
        {"url", "http://localhost:8080/auth"},
        {"message", "Authenticate"}
    };
    
    auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
    
    // Should return decline without calling handler
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
    REQUIRE(handler->url_call_count() == 0);  // Handler should NOT be called
}

TEST_CASE("URL elicitation rejects private IP URLs", "[integration][handlers][security]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingElicitationHandler>();
    handler->set_response({ElicitationAction::Opened, std::nullopt});
    setup.client->set_elicitation_handler(handler);
    
    setup.connect_client();
    
    // Try private IP URL - should be rejected
    Json params = {
        {"mode", "url"},
        {"elicitationId", "test-456"},
        {"url", "http://192.168.1.1/admin"},
        {"message", "Admin panel"}
    };
    
    auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
    REQUIRE(handler->url_call_count() == 0);
}

TEST_CASE("URL elicitation allows valid HTTPS URLs", "[integration][handlers][security]") {
    TestSetup setup;
    
    auto handler = std::make_shared<RecordingElicitationHandler>();
    handler->set_response({ElicitationAction::Opened, std::nullopt});
    setup.client->set_elicitation_handler(handler);
    
    setup.connect_client();
    
    // Valid HTTPS URL should be allowed
    Json params = {
        {"mode", "url"},
        {"elicitationId", "test-789"},
        {"url", "https://example.com/oauth/authorize"},
        {"message", "Authorize"}
    };
    
    auto result = run_sync(setup.io, setup.client->handle_elicitation_request(params));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "opened");
    REQUIRE(handler->url_call_count() == 1);  // Handler SHOULD be called
}

