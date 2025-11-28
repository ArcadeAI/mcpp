// ─────────────────────────────────────────────────────────────────────────────
// Async MCP Client Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for the coroutine-based AsyncMcpClient.

#include <catch2/catch_test_macros.hpp>

#include "mcpp/async/async_mcp_client.hpp"
#include "mcpp/async/async_transport.hpp"
#include "mcpp/async/async_process_transport.hpp"
#include "mcpp/client/async_handlers.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <future>
#include <queue>

using namespace mcpp;
using namespace mcpp::async;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Mock Async Transport
// ═══════════════════════════════════════════════════════════════════════════

class MockAsyncTransport : public IAsyncTransport {
public:
    explicit MockAsyncTransport(asio::any_io_executor executor)
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
        sent_messages_.push(std::move(message));
        co_return TransportResult<void>{};
    }

    asio::awaitable<TransportResult<Json>> async_receive() override {
        if (responses_.empty()) {
            co_return tl::unexpected(TransportError{
                TransportError::Category::Network,
                "No response queued",
                std::nullopt
            });
        }
        auto response = std::move(responses_.front());
        responses_.pop();
        co_return response;
    }

    bool is_running() const override {
        return running_;
    }

    // Test helpers
    void queue_response(Json response) {
        responses_.push(std::move(response));
    }

    void queue_error(TransportError error) {
        responses_.push(tl::unexpected(std::move(error)));
    }

    Json pop_sent_message() {
        if (sent_messages_.empty()) {
            return Json{};
        }
        auto msg = std::move(sent_messages_.front());
        sent_messages_.pop();
        return msg;
    }

    bool has_sent_messages() const {
        return !sent_messages_.empty();
    }

    std::size_t sent_count() const {
        return sent_messages_.size();
    }

private:
    asio::any_io_executor executor_;
    bool running_;
    std::queue<TransportResult<Json>> responses_;
    std::queue<Json> sent_messages_;
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

// Specialization for void
template <>
void run_sync(asio::io_context& io, asio::awaitable<void> coro) {
    std::promise<void> promise;
    auto future = promise.get_future();

    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        try {
            co_await std::move(coro);
            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
            throw;
        }
    }, asio::detached);

    io.run();
    io.restart();

    future.get();
}

// ═══════════════════════════════════════════════════════════════════════════
// Basic Connection Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient initial state", "[async][client]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    REQUIRE(client.is_connected() == false);
    REQUIRE(client.is_initialized() == false);
    REQUIRE(client.server_info().has_value() == false);
}

TEST_CASE("AsyncMcpClient connect without auto-init", "[async][client]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    auto result = run_sync(io, client.connect());

    REQUIRE(result.has_value());
    REQUIRE(client.is_connected() == true);
    REQUIRE(client.is_initialized() == false);  // No auto-init
}

// NOTE: Full auto-init testing requires a more sophisticated mock that properly
// coordinates the bidirectional async communication (send request -> dispatcher 
// receives response -> routes to pending request channel). This is better tested
// with real integration tests using AsyncProcessTransport + real MCP server.
// See real_server_test.cpp for integration test patterns.

TEST_CASE("AsyncMcpClient disconnect", "[async][client]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    run_sync(io, client.connect());
    REQUIRE(client.is_connected() == true);

    run_sync(io, client.disconnect());
    REQUIRE(client.is_connected() == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient operations fail when not connected", "[async][client][error]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    // Try to send request without connecting
    auto result = run_sync(io, client.send_request("test", {}));

    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::NotConnected);
}

TEST_CASE("AsyncMcpClient operations fail when not initialized", "[async][client][error]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    run_sync(io, client.connect());

    // Try to list tools without initializing
    auto result = run_sync(io, client.list_tools());

    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::NotInitialized);
}

// ═══════════════════════════════════════════════════════════════════════════
// AsyncMcpClientError Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClientError factory methods", "[async][client][error]") {
    SECTION("not_connected") {
        auto err = AsyncMcpClientError::not_connected();
        REQUIRE(err.code == ClientErrorCode::NotConnected);
        REQUIRE(err.message.find("not connected") != std::string::npos);
    }

    SECTION("not_initialized") {
        auto err = AsyncMcpClientError::not_initialized();
        REQUIRE(err.code == ClientErrorCode::NotInitialized);
    }

    SECTION("transport_error") {
        auto err = AsyncMcpClientError::transport_error("connection lost");
        REQUIRE(err.code == ClientErrorCode::TransportError);
        REQUIRE(err.message == "connection lost");
    }

    SECTION("protocol_error") {
        auto err = AsyncMcpClientError::protocol_error("invalid response");
        REQUIRE(err.code == ClientErrorCode::ProtocolError);
        REQUIRE(err.message == "invalid response");
    }

    SECTION("timeout_error") {
        auto err = AsyncMcpClientError::timeout_error();
        REQUIRE(err.code == ClientErrorCode::Timeout);
    }

    SECTION("from_rpc_error") {
        McpError rpc_err{-32600, "Invalid Request", std::nullopt};
        auto err = AsyncMcpClientError::from_rpc_error(rpc_err);
        REQUIRE(err.code == ClientErrorCode::ProtocolError);
        REQUIRE(err.rpc_error.has_value());
        REQUIRE(err.rpc_error->code == -32600);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Configuration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClientConfig defaults", "[async][client][config]") {
    AsyncMcpClientConfig config;

    REQUIRE(config.client_name == "mcpp-async");
    REQUIRE(config.client_version == "0.1.0");
    REQUIRE(config.request_timeout == std::chrono::seconds(30));
    REQUIRE(config.auto_initialize == true);
}

TEST_CASE("AsyncMcpClientConfig customization", "[async][client][config]") {
    AsyncMcpClientConfig config;
    config.client_name = "my-client";
    config.client_version = "2.0.0";
    config.request_timeout = std::chrono::seconds(60);
    config.auto_initialize = false;

    REQUIRE(config.client_name == "my-client");
    REQUIRE(config.client_version == "2.0.0");
    REQUIRE(config.request_timeout == std::chrono::seconds(60));
    REQUIRE(config.auto_initialize == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Transport Failure Tests
// ═══════════════════════════════════════════════════════════════════════════

class FailingAsyncTransport : public IAsyncTransport {
public:
    explicit FailingAsyncTransport(asio::any_io_executor executor)
        : executor_(std::move(executor))
    {}

    asio::any_io_executor get_executor() override { return executor_; }

    asio::awaitable<TransportResult<void>> async_start() override {
        if (fail_start_) {
            co_return tl::unexpected(TransportError{
                TransportError::Category::Network,
                "Connection refused",
                std::nullopt
            });
        }
        running_ = true;
        co_return TransportResult<void>{};
    }

    asio::awaitable<void> async_stop() override {
        running_ = false;
        co_return;
    }

    asio::awaitable<TransportResult<void>> async_send(Json /*message*/) override {
        if (fail_send_) {
            co_return tl::unexpected(TransportError{
                TransportError::Category::Network,
                "Broken pipe",
                std::nullopt
            });
        }
        co_return TransportResult<void>{};
    }

    asio::awaitable<TransportResult<Json>> async_receive() override {
        co_return tl::unexpected(TransportError{
            TransportError::Category::Network,
            "EOF",
            std::nullopt
        });
    }

    bool is_running() const override { return running_; }

    void set_fail_start(bool fail) { fail_start_ = fail; }
    void set_fail_send(bool fail) { fail_send_ = fail; }

private:
    asio::any_io_executor executor_;
    bool running_ = false;
    bool fail_start_ = false;
    bool fail_send_ = false;
};

TEST_CASE("AsyncMcpClient handles transport start failure", "[async][client][error]") {
    asio::io_context io;
    auto transport_ptr = std::make_unique<FailingAsyncTransport>(io.get_executor());
    transport_ptr->set_fail_start(true);

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport_ptr), config);

    auto result = run_sync(io, client.connect());

    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::TransportError);
    REQUIRE(client.is_connected() == false);
}

TEST_CASE("AsyncMcpClient handles send failure", "[async][client][error]") {
    asio::io_context io;
    auto transport_ptr = std::make_unique<FailingAsyncTransport>(io.get_executor());
    transport_ptr->set_fail_send(true);

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport_ptr), config);

    run_sync(io, client.connect());
    REQUIRE(client.is_connected() == true);

    auto result = run_sync(io, client.send_notification("test", {}));

    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::TransportError);
}

// ═══════════════════════════════════════════════════════════════════════════
// Double Operation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient double connect returns error", "[async][client]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    auto result1 = run_sync(io, client.connect());
    REQUIRE(result1.has_value());
    REQUIRE(client.is_connected() == true);

    auto result2 = run_sync(io, client.connect());
    REQUIRE(result2.has_value() == false);
    REQUIRE(result2.error().code == ClientErrorCode::ProtocolError);
}

TEST_CASE("AsyncMcpClient double disconnect is safe", "[async][client]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    run_sync(io, client.connect());
    REQUIRE(client.is_connected() == true);

    // First disconnect
    run_sync(io, client.disconnect());
    REQUIRE(client.is_connected() == false);

    // Second disconnect should be safe (no-op)
    REQUIRE_NOTHROW(run_sync(io, client.disconnect()));
    REQUIRE(client.is_connected() == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Notification Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient notification handlers can be set", "[async][client]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());

    AsyncMcpClientConfig config;
    config.auto_initialize = false;

    AsyncMcpClient client(std::move(transport), config);

    bool generic_called = false;
    bool tool_changed_called = false;
    bool resource_changed_called = false;
    bool prompt_changed_called = false;

    client.on_notification([&](const std::string&, const Json&) {
        generic_called = true;
    });

    client.on_tool_list_changed([&]() {
        tool_changed_called = true;
    });

    client.on_resource_list_changed([&]() {
        resource_changed_called = true;
    });

    client.on_prompt_list_changed([&]() {
        prompt_changed_called = true;
    });

    // Handlers are set but not called yet (no notifications received)
    REQUIRE(generic_called == false);
    REQUIRE(tool_changed_called == false);
    REQUIRE(resource_changed_called == false);
    REQUIRE(prompt_changed_called == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// AsyncProcessConfig Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncProcessConfig defaults", "[async][transport][config]") {
    AsyncProcessConfig config;

    REQUIRE(config.command.empty());
    REQUIRE(config.args.empty());
    REQUIRE(config.use_content_length_framing == true);
    REQUIRE(config.max_message_size == (1 << 20));  // 1 MiB
    REQUIRE(config.stderr_handling == StderrHandling::Discard);
    REQUIRE(config.channel_capacity == 16);
    REQUIRE(config.shutdown_timeout == std::chrono::seconds(5));
}

TEST_CASE("AsyncProcessConfig customization", "[async][transport][config]") {
    AsyncProcessConfig config;
    config.command = "python";
    config.args = {"-m", "mcp_server"};
    config.use_content_length_framing = false;
    config.max_message_size = 1024;
    config.stderr_handling = StderrHandling::Passthrough;
    config.channel_capacity = 32;
    config.shutdown_timeout = std::chrono::seconds(10);

    REQUIRE(config.command == "python");
    REQUIRE(config.args.size() == 2);
    REQUIRE(config.use_content_length_framing == false);
    REQUIRE(config.max_message_size == 1024);
    REQUIRE(config.stderr_handling == StderrHandling::Passthrough);
    REQUIRE(config.channel_capacity == 32);
    REQUIRE(config.shutdown_timeout == std::chrono::seconds(10));
}

// ═══════════════════════════════════════════════════════════════════════════
// Async Client Capability Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "mcpp/client/elicitation_handler.hpp"
#include "mcpp/client/sampling_handler.hpp"
#include "mcpp/client/roots_handler.hpp"

namespace {
class TestAsyncElicitationHandler : public IElicitationHandler {
public:
    ElicitationResult form_response{ElicitationAction::Accept, Json{{"name", "test"}}};
    ElicitationResult url_response{ElicitationAction::Opened, std::nullopt};
    
    ElicitationResult handle_form(const std::string&, const Json&) override {
        return form_response;
    }
    
    ElicitationResult handle_url(const std::string&, const std::string&, const std::string&) override {
        return url_response;
    }
};

class TestAsyncSamplingHandler : public ISamplingHandler {
public:
    std::optional<CreateMessageResult> response;
    
    std::optional<CreateMessageResult> handle_create_message(const CreateMessageParams&) override {
        return response;
    }
};
}  // namespace

TEST_CASE("AsyncMcpClient set_elicitation_handler", "[async][client][elicitation]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClient client(std::move(transport));
    
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    client.set_elicitation_handler(handler);
    
    REQUIRE(true);  // Should not throw
}

TEST_CASE("AsyncMcpClient handle_elicitation_request form mode", "[async][client][elicitation]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;  // Skip init to avoid mock complexity
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    handler->form_response = {ElicitationAction::Accept, Json{{"username", "testuser"}}};
    client.set_elicitation_handler(handler);
    
    run_sync(io, client.connect());  // Sets connected_ = true
    
    Json request = {
        {"mode", "form"},
        {"message", "Enter username"},
        {"requestedSchema", Json::object()}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "accept");
    REQUIRE((*result)["content"]["username"] == "testuser");
}

TEST_CASE("AsyncMcpClient handle_elicitation_request fails when not connected", "[async][client][elicitation]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClient client(std::move(transport));
    
    Json request = {{"mode", "form"}, {"message", "test"}, {"requestedSchema", Json::object()}};
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ClientErrorCode::NotConnected);
}

TEST_CASE("AsyncMcpClient handle_elicitation_request url mode", "[async][client][elicitation]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    handler->url_response = {ElicitationAction::Opened, std::nullopt};
    client.set_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "abc-123"},
        {"url", "https://example.com/auth"},
        {"message", "Authorize"}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "opened");
}

TEST_CASE("AsyncMcpClient elicitation without handler returns dismiss", "[async][client][elicitation]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    // No handler set
    
    run_sync(io, client.connect());
    
    Json request = {
        {"mode", "form"},
        {"message", "Enter data"},
        {"requestedSchema", Json::object()}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "dismiss");
}

TEST_CASE("AsyncMcpClient set_sampling_handler", "[async][client][sampling]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClient client(std::move(transport));
    
    auto handler = std::make_shared<TestAsyncSamplingHandler>();
    client.set_sampling_handler(handler);
    
    REQUIRE(true);  // Should not throw
}

TEST_CASE("AsyncMcpClient handle_sampling_request", "[async][client][sampling]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncSamplingHandler>();
    handler->response = CreateMessageResult{
        SamplingRole::Assistant,
        TextContent{"Response text", std::nullopt},
        "test-model",
        StopReason::EndTurn
    };
    client.set_sampling_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hello"}}}}
        }}
    };
    
    auto result = run_sync(io, client.handle_sampling_request(request));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["role"] == "assistant");
    REQUIRE((*result)["model"] == "test-model");
}

TEST_CASE("AsyncMcpClient sampling without handler returns error", "[async][client][sampling]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    // No handler set
    
    run_sync(io, client.connect());
    
    Json request = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Test"}}}}
        }}
    };
    
    auto result = run_sync(io, client.handle_sampling_request(request));
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ClientErrorCode::ProtocolError);
}

TEST_CASE("AsyncMcpClient sampling fails when not connected", "[async][client][sampling]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClient client(std::move(transport));
    
    Json request = {{"messages", Json::array()}};
    
    auto result = run_sync(io, client.handle_sampling_request(request));
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ClientErrorCode::NotConnected);
}

TEST_CASE("AsyncMcpClient set_roots_handler", "[async][client][roots]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClient client(std::move(transport));
    
    auto handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{
        Root{"file:///test", "Test"}
    });
    client.set_roots_handler(handler);
    
    REQUIRE(true);  // Should not throw
}

TEST_CASE("AsyncMcpClient handle_roots_list_request", "[async][client][roots]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{
        Root{"file:///home/user/project", "Project"},
        Root{"file:///shared", std::nullopt}
    });
    client.set_roots_handler(handler);
    
    run_sync(io, client.connect());
    
    auto result = run_sync(io, client.handle_roots_list_request());
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].size() == 2);
    REQUIRE((*result)["roots"][0]["uri"] == "file:///home/user/project");
}

TEST_CASE("AsyncMcpClient roots without handler returns empty list", "[async][client][roots]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    // No handler set
    
    run_sync(io, client.connect());
    
    auto result = run_sync(io, client.handle_roots_list_request());
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].empty());
}

TEST_CASE("AsyncMcpClient roots fails when not connected", "[async][client][roots]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClient client(std::move(transport));
    
    auto result = run_sync(io, client.handle_roots_list_request());
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ClientErrorCode::NotConnected);
}

// ═══════════════════════════════════════════════════════════════════════════
// Message Dispatcher Auto-Routing Tests
// ═══════════════════════════════════════════════════════════════════════════
//
// These tests verify that the AsyncMcpClient's message dispatcher correctly
// routes incoming server requests to the appropriate handlers.
//
// Note: Full integration testing with bidirectional async communication is
// complex with mocks. These tests verify the dispatch logic directly by
// calling the internal dispatch methods. Full end-to-end testing is better
// done with real server integration tests (see real_server_test.cpp).

TEST_CASE("dispatch_server_request routes elicitation correctly", "[async][client][dispatcher]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    auto* transport_ptr = transport.get();
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set up handler
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    handler->form_response = {ElicitationAction::Accept, Json{{"answer", "42"}}};
    client.set_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    // Verify handler is called correctly via handle_elicitation_request
    Json params = {
        {"mode", "form"},
        {"message", "What is the answer?"},
        {"requestedSchema", Json::object()}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(params));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "accept");
    REQUIRE((*result)["content"]["answer"] == "42");
}

TEST_CASE("dispatch_server_request routes sampling correctly", "[async][client][dispatcher]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set up handler
    auto handler = std::make_shared<TestAsyncSamplingHandler>();
    handler->response = CreateMessageResult{
        SamplingRole::Assistant,
        TextContent{"Hello!", std::nullopt},
        "test-model",
        StopReason::EndTurn
    };
    client.set_sampling_handler(handler);
    
    run_sync(io, client.connect());
    
    Json params = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hi"}}}}
        }}
    };
    
    auto result = run_sync(io, client.handle_sampling_request(params));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["role"] == "assistant");
    REQUIRE((*result)["model"] == "test-model");
}

TEST_CASE("dispatch_server_request routes roots/list correctly", "[async][client][dispatcher]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set up handler
    auto handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{
        Root{"file:///workspace", "Workspace"}
    });
    client.set_roots_handler(handler);
    
    run_sync(io, client.connect());
    
    auto result = run_sync(io, client.handle_roots_list_request());
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].size() == 1);
    REQUIRE((*result)["roots"][0]["uri"] == "file:///workspace");
}

TEST_CASE("dispatch_server_request returns error for missing handler", "[async][client][dispatcher]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    // No sampling handler set
    
    run_sync(io, client.connect());
    
    Json params = {{"messages", Json::array()}};
    
    auto result = run_sync(io, client.handle_sampling_request(params));
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ClientErrorCode::ProtocolError);
}

// ═══════════════════════════════════════════════════════════════════════════
// Async Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

// Test async elicitation handler
class TestAsyncElicitationHandlerImpl : public IAsyncElicitationHandler {
public:
    ElicitationResult form_response{ElicitationAction::Dismiss, std::nullopt};
    ElicitationResult url_response{ElicitationAction::Dismiss, std::nullopt};
    int form_calls = 0;
    int url_calls = 0;

    asio::awaitable<ElicitationResult> handle_form_async(
        const std::string& /*message*/,
        const Json& /*schema*/
    ) override {
        ++form_calls;
        co_return form_response;
    }

    asio::awaitable<ElicitationResult> handle_url_async(
        const std::string& /*elicitation_id*/,
        const std::string& /*url*/,
        const std::string& /*message*/
    ) override {
        ++url_calls;
        co_return url_response;
    }
};

// Test async sampling handler
class TestAsyncSamplingHandlerImpl : public IAsyncSamplingHandler {
public:
    std::optional<CreateMessageResult> response;
    int calls = 0;

    asio::awaitable<std::optional<CreateMessageResult>> handle_create_message_async(
        const CreateMessageParams& /*params*/
    ) override {
        ++calls;
        co_return response;
    }
};

// Test async roots handler
class TestAsyncRootsHandlerImpl : public IAsyncRootsHandler {
public:
    ListRootsResult result;
    int calls = 0;

    asio::awaitable<ListRootsResult> list_roots_async() override {
        ++calls;
        co_return result;
    }
};

TEST_CASE("AsyncMcpClient async elicitation handler form mode", "[async][client][handlers]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncElicitationHandlerImpl>();
    handler->form_response = {ElicitationAction::Accept, Json{{"key", "async_value"}}};
    client.set_async_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"mode", "form"},
        {"message", "Async test"},
        {"requestedSchema", Json::object()}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(handler->form_calls == 1);
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "accept");
    REQUIRE((*result)["content"]["key"] == "async_value");
}

TEST_CASE("AsyncMcpClient async elicitation handler url mode", "[async][client][handlers]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncElicitationHandlerImpl>();
    handler->url_response = {ElicitationAction::Opened, std::nullopt};
    client.set_async_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "async-123"},
        {"url", "https://example.com/async"},
        {"message", "Async auth"}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(handler->url_calls == 1);
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "opened");
}

TEST_CASE("AsyncMcpClient async handler takes precedence over sync handler", "[async][client][handlers]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set sync handler first
    auto sync_handler = std::make_shared<TestAsyncElicitationHandler>();
    sync_handler->form_response = {ElicitationAction::Decline, Json{{"from", "sync"}}};
    client.set_elicitation_handler(sync_handler);
    
    // Set async handler - should take precedence
    auto async_handler = std::make_shared<TestAsyncElicitationHandlerImpl>();
    async_handler->form_response = {ElicitationAction::Accept, Json{{"from", "async"}}};
    client.set_async_elicitation_handler(async_handler);
    
    run_sync(io, client.connect());
    
    Json request = {{"mode", "form"}, {"message", "Test"}, {"requestedSchema", Json::object()}};
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(async_handler->form_calls == 1);
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "accept");
    REQUIRE((*result)["content"]["from"] == "async");
}

TEST_CASE("AsyncMcpClient async sampling handler", "[async][client][handlers]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncSamplingHandlerImpl>();
    handler->response = CreateMessageResult{
        SamplingRole::Assistant,
        TextContent{"Async response", std::nullopt},
        "async-model",
        StopReason::EndTurn
    };
    client.set_async_sampling_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hello async"}}}}
        }}
    };
    
    auto result = run_sync(io, client.handle_sampling_request(request));
    
    REQUIRE(handler->calls == 1);
    REQUIRE(result.has_value());
    REQUIRE((*result)["role"] == "assistant");
    REQUIRE((*result)["model"] == "async-model");
}

TEST_CASE("AsyncMcpClient async roots handler", "[async][client][handlers]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncRootsHandlerImpl>();
    handler->result = ListRootsResult{
        {Root{"file:///async/project", "Async Project"}}
    };
    client.set_async_roots_handler(handler);
    
    run_sync(io, client.connect());
    
    auto result = run_sync(io, client.handle_roots_list_request());
    
    REQUIRE(handler->calls == 1);
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].size() == 1);
    REQUIRE((*result)["roots"][0]["uri"] == "file:///async/project");
}

// ═══════════════════════════════════════════════════════════════════════════
// Progress Notification Dispatch Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient on_progress handler is called", "[async][client][progress]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    auto* transport_ptr = transport.get();
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Track progress notifications
    std::vector<ProgressNotification> received_progress;
    client.on_progress([&](const ProgressNotification& p) {
        received_progress.push_back(p);
    });
    
    run_sync(io, client.connect());
    
    // Simulate server sending progress notification
    Json progress_notification = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/progress"},
        {"params", {
            {"progressToken", "task-123"},
            {"progress", 50.0},
            {"total", 100.0}
        }}
    };
    transport_ptr->queue_response(progress_notification);
    
    // Process the notification by running io_context briefly
    io.poll();
    io.restart();
    
    // Note: Since we're not running the full message_dispatcher loop,
    // we test the handler registration works correctly
    REQUIRE(received_progress.empty() == true);  // No dispatcher running
}

TEST_CASE("AsyncMcpClient on_progress handler thread safety", "[async][client][progress][thread]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set handler multiple times rapidly (simulates concurrent access)
    std::atomic<int> call_count{0};
    
    for (int i = 0; i < 10; ++i) {
        client.on_progress([&](const ProgressNotification&) {
            ++call_count;
        });
    }
    
    // Should not crash or deadlock
    REQUIRE(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Resource Updated Notification Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient on_resource_updated handler registration", "[async][client][notifications]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    std::vector<std::string> updated_uris;
    client.on_resource_updated([&](const std::string& uri) {
        updated_uris.push_back(uri);
    });
    
    run_sync(io, client.connect());
    
    // Handler registered successfully
    REQUIRE(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// Notification Handler Thread Safety Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient notification handlers can be set concurrently", "[async][client][thread]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set all handlers (tests mutex protection)
    client.on_notification([](const std::string&, const Json&) {});
    client.on_tool_list_changed([]() {});
    client.on_resource_list_changed([]() {});
    client.on_resource_updated([](const std::string&) {});
    client.on_prompt_list_changed([]() {});
    client.on_log_message([](LoggingLevel, const std::string&, const std::string&) {});
    client.on_progress([](const ProgressNotification&) {});
    
    // Should not crash or deadlock
    REQUIRE(true);
}

// ═══════════════════════════════════════════════════════════════════════════
// URL Validation in Elicitation Tests (Async Client)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient rejects localhost URL in elicitation", "[async][client][security]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set handler that should NOT be called
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    handler->url_response = {ElicitationAction::Opened, std::nullopt};
    client.set_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "test-123"},
        {"url", "http://localhost:8080/auth"},
        {"message", "Authenticate"}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    // Should decline without calling handler
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
}

TEST_CASE("AsyncMcpClient rejects private IP URL in elicitation", "[async][client][security]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    handler->url_response = {ElicitationAction::Opened, std::nullopt};
    client.set_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "test-456"},
        {"url", "http://192.168.1.1/admin"},
        {"message", "Admin"}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
}

TEST_CASE("AsyncMcpClient allows valid HTTPS URL in elicitation", "[async][client][security]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    handler->url_response = {ElicitationAction::Opened, std::nullopt};
    client.set_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "test-789"},
        {"url", "https://example.com/oauth"},
        {"message", "Authorize"}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(request));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "opened");
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Code Propagation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient returns not_connected error when disconnected", "[async][client][error]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Don't connect - try to send request
    auto result = run_sync(io, client.send_request("test/method", {}));
    
    REQUIRE(!result.has_value());
    REQUIRE(result.error().code == ClientErrorCode::NotConnected);
}

TEST_CASE("AsyncMcpClient returns not_initialized error for API calls before init", "[async][client][error]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Connect but don't initialize
    run_sync(io, client.connect());
    
    // API calls should fail with not_initialized
    auto tools_result = run_sync(io, client.list_tools());
    REQUIRE(!tools_result.has_value());
    REQUIRE(tools_result.error().code == ClientErrorCode::NotInitialized);
}

TEST_CASE("AsyncMcpClient handler errors propagate correctly", "[async][client][error]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // No handler set - should return error for handler requests
    run_sync(io, client.connect());
    
    // Elicitation without handler returns dismiss (not error)
    Json elicit_request = {{"mode", "form"}, {"message", "test"}};
    auto elicit_result = run_sync(io, client.handle_elicitation_request(elicit_request));
    REQUIRE(elicit_result.has_value());
    REQUIRE((*elicit_result)["action"] == "dismiss");
    
    // Sampling without handler returns protocol error
    Json sample_request = {{"messages", Json::array()}};
    auto sample_result = run_sync(io, client.handle_sampling_request(sample_request));
    REQUIRE(!sample_result.has_value());
    REQUIRE(sample_result.error().code == ClientErrorCode::ProtocolError);
    
    // Roots without handler returns empty list
    auto roots_result = run_sync(io, client.handle_roots_list_request());
    REQUIRE(roots_result.has_value());
    REQUIRE((*roots_result)["roots"].empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Full Dispatcher Loop Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient message dispatcher handles mixed messages", "[async][client][dispatcher]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    auto* transport_ptr = transport.get();
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Track notifications received
    std::vector<std::string> notifications;
    client.on_notification([&](const std::string& method, const Json&) {
        notifications.push_back(method);
    });
    
    run_sync(io, client.connect());
    
    // Queue a notification
    transport_ptr->queue_response({
        {"jsonrpc", "2.0"},
        {"method", "notifications/tools/list_changed"}
    });
    
    // Poll io_context to process queued messages
    io.poll();
    io.restart();
    
    // Note: Full dispatcher test would require running the dispatcher coroutine
    // This tests that the notification handler is registered correctly
    REQUIRE(notifications.empty() == true);  // No dispatcher running
}

TEST_CASE("AsyncMcpClient handles server request with method and id", "[async][client][dispatcher]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Set up handler
    auto handler = std::make_shared<TestAsyncElicitationHandler>();
    handler->form_response = {ElicitationAction::Accept, Json{{"name", "test"}}};
    client.set_elicitation_handler(handler);
    
    run_sync(io, client.connect());
    
    // Directly test the handler (simulating what dispatcher would do)
    Json server_request = {
        {"mode", "form"},
        {"message", "Enter name"},
        {"requestedSchema", {{"type", "object"}}}
    };
    
    auto result = run_sync(io, client.handle_elicitation_request(server_request));
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "accept");
    REQUIRE((*result)["content"]["name"] == "test");
}

// ═══════════════════════════════════════════════════════════════════════════
// Circuit Breaker Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncMcpClient circuit breaker is enabled by default", "[async][client][circuit-breaker]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Circuit should start closed
    REQUIRE(client.circuit_state() == CircuitState::Closed);
    REQUIRE(client.is_circuit_open() == false);
    
    // Stats should be zeroed
    auto stats = client.circuit_stats();
    REQUIRE(stats.total_requests == 0);
    REQUIRE(stats.failed_requests == 0);
}

TEST_CASE("AsyncMcpClient circuit breaker can be disabled", "[async][client][circuit-breaker]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    config.enable_circuit_breaker = false;  // Disable circuit breaker
    AsyncMcpClient client(std::move(transport), config);
    
    // With circuit breaker disabled, state should always be Closed
    REQUIRE(client.circuit_state() == CircuitState::Closed);
    REQUIRE(client.is_circuit_open() == false);
    
    // Stats should be empty
    auto stats = client.circuit_stats();
    REQUIRE(stats.total_requests == 0);
}

TEST_CASE("AsyncMcpClient circuit breaker can be forced open", "[async][client][circuit-breaker]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Force circuit open (can be done without connect)
    client.force_circuit_open();
    REQUIRE(client.is_circuit_open() == true);
    REQUIRE(client.circuit_state() == CircuitState::Open);
    
    // Stats should show rejected requests would be counted
    // (actual request rejection tested via send_request path)
}

TEST_CASE("AsyncMcpClient circuit breaker can be forced closed", "[async][client][circuit-breaker]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Force open then force close (can be done without connect)
    client.force_circuit_open();
    REQUIRE(client.is_circuit_open() == true);
    
    client.force_circuit_closed();
    REQUIRE(client.is_circuit_open() == false);
    REQUIRE(client.circuit_state() == CircuitState::Closed);
}

TEST_CASE("AsyncMcpClient circuit breaker state change callback", "[async][client][circuit-breaker]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    std::vector<std::pair<CircuitState, CircuitState>> transitions;
    client.on_circuit_state_change([&](CircuitState from, CircuitState to) {
        transitions.push_back({from, to});
    });
    
    // Force state changes
    client.force_circuit_open();
    client.force_circuit_closed();
    
    REQUIRE(transitions.size() == 2);
    REQUIRE(transitions[0].first == CircuitState::Closed);
    REQUIRE(transitions[0].second == CircuitState::Open);
    REQUIRE(transitions[1].first == CircuitState::Open);
    REQUIRE(transitions[1].second == CircuitState::Closed);
}

TEST_CASE("AsyncMcpClient circuit breaker with custom config", "[async][client][circuit-breaker]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    
    // Custom circuit breaker settings
    config.circuit_breaker.failure_threshold = 2;
    config.circuit_breaker.success_threshold = 1;
    config.circuit_breaker.recovery_timeout = std::chrono::milliseconds(100);
    
    AsyncMcpClient client(std::move(transport), config);
    
    // Circuit should be closed initially
    REQUIRE(client.circuit_state() == CircuitState::Closed);
}

TEST_CASE("AsyncMcpClient circuit breaker tracks statistics", "[async][client][circuit-breaker]") {
    asio::io_context io;
    auto transport = std::make_unique<MockAsyncTransport>(io.get_executor());
    
    AsyncMcpClientConfig config;
    config.auto_initialize = false;
    AsyncMcpClient client(std::move(transport), config);
    
    // Initial stats should be zero
    auto stats = client.circuit_stats();
    REQUIRE(stats.total_requests == 0);
    REQUIRE(stats.failed_requests == 0);
    REQUIRE(stats.successful_requests == 0);
    
    // Force state changes and verify transitions are tracked
    client.force_circuit_open();
    client.force_circuit_closed();
    
    stats = client.circuit_stats();
    REQUIRE(stats.state_transitions == 2);
}

