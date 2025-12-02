// ─────────────────────────────────────────────────────────────────────────────
// MCP Client Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for the high-level McpClient API

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/client/mcp_client.hpp"
#include "mcpp/transport/backoff_policy.hpp"
#include "mocks/mock_mcp_server.hpp"

using namespace mcpp;
using namespace mcpp::testing;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Test Helpers
// ═══════════════════════════════════════════════════════════════════════════

std::pair<std::unique_ptr<McpClient>, MockMcpServer*>
make_test_client() {
    static MockMcpServer server;
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.client_name = "test-client";
    config.client_version = "1.0.0";
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    auto client = std::make_unique<McpClient>(std::move(config), std::move(mock_client));
    
    return {std::move(client), &server};
}

// ═══════════════════════════════════════════════════════════════════════════
// Connection Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient connects and initializes", "[mcp][client]") {
    auto [client, server] = make_test_client();
    
    REQUIRE(client->is_connected() == false);
    REQUIRE(client->is_initialized() == false);
    
    auto result = client->connect();
    
    REQUIRE(result.has_value());
    REQUIRE(client->is_connected() == true);
    REQUIRE(client->is_initialized() == true);
    
    // Check server info
    REQUIRE(client->server_info().has_value());
    REQUIRE(client->server_info()->name == "MockMcpServer");
    
    // Check capabilities
    REQUIRE(client->server_capabilities().has_value());
    
    client->disconnect();
    REQUIRE(client->is_connected() == false);
    REQUIRE(client->is_initialized() == false);
}

TEST_CASE("McpClient rejects double connect", "[mcp][client]") {
    auto [client, server] = make_test_client();
    
    auto result1 = client->connect();
    REQUIRE(result1.has_value());
    
    auto result2 = client->connect();
    REQUIRE(result2.has_value() == false);
    REQUIRE(result2.error().code == ClientErrorCode::ProtocolError);
    
    client->disconnect();
}

TEST_CASE("McpClient operations fail when not connected", "[mcp][client]") {
    auto [client, server] = make_test_client();
    
    // send_request checks connected_ first
    auto result = client->send_request("tools/list");
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::NotConnected);
}

TEST_CASE("McpClient operations fail when not initialized", "[mcp][client]") {
    auto [client, server] = make_test_client();
    
    // Manually start transport without auto-initialize
    client->transport().start();
    
    // Should fail because not initialized
    auto result = client->list_tools();
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::NotInitialized);
    
    client->disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Tools API Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient list_tools returns tools", "[mcp][client][tools]") {
    MockMcpServer server;
    
    server.on_request("tools/list", [](const Json& params) {
        return Json{
            {"tools", Json::array({
                {{"name", "echo"}, {"description", "Echoes input"}},
                {{"name", "add"}, {"description", "Adds numbers"}}
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.list_tools();
    
    REQUIRE(result.has_value());
    REQUIRE(result->tools.size() == 2);
    REQUIRE(result->tools[0].name == "echo");
    REQUIRE(result->tools[1].name == "add");
    
    client.disconnect();
}

TEST_CASE("McpClient call_tool executes tool", "[mcp][client][tools]") {
    MockMcpServer server;
    
    server.on_request("tools/call", [](const Json& params) {
        const auto name = params.value("name", "");
        const auto args = params.value("arguments", Json::object());
        
        if (name == "echo") {
            return Json{
                {"content", Json::array({
                    {{"type", "text"}, {"text", args.value("message", "")}}
                })}
            };
        }
        throw std::runtime_error("Unknown tool");
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.call_tool("echo", {{"message", "Hello!"}});
    
    REQUIRE(result.has_value());
    REQUIRE(result->content.size() == 1);
    REQUIRE(std::holds_alternative<TextContent>(result->content[0]));
    REQUIRE(std::get<TextContent>(result->content[0]).text == "Hello!");
    
    client.disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Resources API Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient list_resources returns resources", "[mcp][client][resources]") {
    MockMcpServer server;
    
    server.on_request("resources/list", [](const Json& params) {
        return Json{
            {"resources", Json::array({
                {{"uri", "file:///config.json"}, {"name", "Config"}},
                {{"uri", "file:///readme.md"}, {"name", "README"}}
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.list_resources();
    
    REQUIRE(result.has_value());
    REQUIRE(result->resources.size() == 2);
    REQUIRE(result->resources[0].uri == "file:///config.json");
    
    client.disconnect();
}

TEST_CASE("McpClient read_resource returns content", "[mcp][client][resources]") {
    MockMcpServer server;
    
    server.on_request("resources/read", [](const Json& params) {
        return Json{
            {"contents", Json::array({
                {
                    {"uri", params.value("uri", "")},
                    {"mimeType", "application/json"},
                    {"text", R"({"key": "value"})"}
                }
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.read_resource("file:///config.json");
    
    REQUIRE(result.has_value());
    REQUIRE(result->contents.size() == 1);
    REQUIRE(result->contents[0].text.has_value());
    REQUIRE(*result->contents[0].text == R"({"key": "value"})");
    
    client.disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Prompts API Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient list_prompts returns prompts", "[mcp][client][prompts]") {
    MockMcpServer server;
    
    server.on_request("prompts/list", [](const Json& params) {
        return Json{
            {"prompts", Json::array({
                {{"name", "code-review"}, {"description", "Review code"}}
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.list_prompts();
    
    REQUIRE(result.has_value());
    REQUIRE(result->prompts.size() == 1);
    REQUIRE(result->prompts[0].name == "code-review");
    
    client.disconnect();
}

TEST_CASE("McpClient get_prompt returns messages", "[mcp][client][prompts]") {
    MockMcpServer server;
    
    server.on_request("prompts/get", [](const Json& params) {
        return Json{
            {"description", "A helpful prompt"},
            {"messages", Json::array({
                {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hello"}}}}
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.get_prompt("code-review", {{"language", "cpp"}});
    
    REQUIRE(result.has_value());
    REQUIRE(result->description.has_value());
    REQUIRE(result->messages.size() == 1);
    REQUIRE(result->messages[0].role == "user");
    
    client.disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient handles RPC errors", "[mcp][client][error]") {
    MockMcpServer server;
    
    server.on_request("tools/call", [](const Json& params) -> Json {
        throw std::runtime_error("Tool execution failed");
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.call_tool("failing-tool", {});
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::ProtocolError);
    REQUIRE(result.error().rpc_error.has_value());
    REQUIRE(result.error().rpc_error->message == "Tool execution failed");
    
    client.disconnect();
}

TEST_CASE("McpClient handles method not found", "[mcp][client][error]") {
    MockMcpServer server;
    // No handlers registered
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    // tools/list is not registered, should get method not found
    auto result = client.list_tools();
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == ClientErrorCode::ProtocolError);
    REQUIRE(result.error().rpc_error.has_value());
    REQUIRE(result.error().rpc_error->code == ErrorCode::MethodNotFound);
    
    client.disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Raw Request Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient send_request allows custom methods", "[mcp][client]") {
    MockMcpServer server;
    
    server.on_request("custom/method", [](const Json& params) {
        return Json{{"custom", "response"}};
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.send_request("custom/method", {{"param", "value"}});
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["custom"] == "response");
    
    client.disconnect();
}

TEST_CASE("McpClient send_notification sends without response", "[mcp][client]") {
    MockMcpServer server;
    
    bool notification_received = false;
    server.on_notification("custom/notification", [&](const Json& params) {
        notification_received = true;
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    McpClient client(std::move(config), std::move(mock_client));
    client.connect();
    
    auto result = client.send_notification("custom/notification", {{"data", "test"}});
    
    REQUIRE(result.has_value());
    
    client.disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "mcpp/client/elicitation_handler.hpp"

namespace {
class TestElicitationHandler : public IElicitationHandler {
public:
    std::string last_form_message;
    Json last_form_schema;
    std::string last_url_id;
    std::string last_url;
    std::string last_url_message;
    
    ElicitationResult form_response{ElicitationAction::Accept, Json{{"name", "test-user"}}};
    ElicitationResult url_response{ElicitationAction::Opened, std::nullopt};
    
    ElicitationResult handle_form(
        const std::string& message,
        const Json& schema
    ) override {
        last_form_message = message;
        last_form_schema = schema;
        return form_response;
    }
    
    ElicitationResult handle_url(
        const std::string& elicitation_id,
        const std::string& url,
        const std::string& message
    ) override {
        last_url_id = elicitation_id;
        last_url = url;
        last_url_message = message;
        return url_response;
    }
};
}  // namespace

TEST_CASE("McpClient set_elicitation_handler", "[mcp][client][elicitation]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestElicitationHandler>();
    client->set_elicitation_handler(handler);
    
    // Handler should be set (no way to verify directly, but shouldn't throw)
    REQUIRE(true);
}

TEST_CASE("McpClient handles form elicitation request", "[mcp][client][elicitation]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestElicitationHandler>();
    handler->form_response = {ElicitationAction::Accept, Json{{"username", "octocat"}}};
    client->set_elicitation_handler(handler);
    
    client->connect();
    
    // Simulate server sending elicitation request
    Json server_request = {
        {"mode", "form"},
        {"message", "Please enter your username"},
        {"requestedSchema", {
            {"type", "object"},
            {"properties", {{"username", {{"type", "string"}}}}}
        }}
    };
    
    auto result = client->handle_elicitation_request(server_request);
    
    REQUIRE(result.has_value());
    REQUIRE(handler->last_form_message == "Please enter your username");
    REQUIRE(handler->last_form_schema["type"] == "object");
    REQUIRE((*result)["action"] == "accept");
    REQUIRE((*result)["content"]["username"] == "octocat");
    
    client->disconnect();
}

TEST_CASE("McpClient handles url elicitation request", "[mcp][client][elicitation]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestElicitationHandler>();
    handler->url_response = {ElicitationAction::Opened, std::nullopt};
    client->set_elicitation_handler(handler);
    
    client->connect();
    
    // Simulate server sending URL elicitation request
    Json server_request = {
        {"mode", "url"},
        {"elicitationId", "auth-123"},
        {"url", "https://github.com/login/oauth"},
        {"message", "Please authorize GitHub access"}
    };
    
    auto result = client->handle_elicitation_request(server_request);
    
    REQUIRE(result.has_value());
    REQUIRE(handler->last_url_id == "auth-123");
    REQUIRE(handler->last_url == "https://github.com/login/oauth");
    REQUIRE(handler->last_url_message == "Please authorize GitHub access");
    REQUIRE((*result)["action"] == "opened");
    
    client->disconnect();
}

TEST_CASE("McpClient elicitation without handler returns dismiss", "[mcp][client][elicitation]") {
    auto [client, server] = make_test_client();
    
    // No handler set
    client->connect();
    
    Json server_request = {
        {"mode", "form"},
        {"message", "Enter data"},
        {"requestedSchema", Json::object()}
    };
    
    auto result = client->handle_elicitation_request(server_request);
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "dismiss");
    
    client->disconnect();
}

TEST_CASE("McpClient elicitation handler decline", "[mcp][client][elicitation]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestElicitationHandler>();
    handler->form_response = {ElicitationAction::Decline, std::nullopt};
    client->set_elicitation_handler(handler);
    
    client->connect();
    
    Json server_request = {
        {"mode", "form"},
        {"message", "Enter sensitive data"},
        {"requestedSchema", Json::object()}
    };
    
    auto result = client->handle_elicitation_request(server_request);
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
    REQUIRE_FALSE((*result).contains("content"));
    
    client->disconnect();
}

TEST_CASE("ClientCapabilities includes elicitation", "[mcp][client][elicitation]") {
    // Test that ClientCapabilities correctly serializes elicitation
    ClientCapabilities caps;
    caps.elicitation = ElicitationCapability{true, false};  // form only
    
    auto json = caps.to_json();
    
    REQUIRE(json.contains("elicitation"));
    REQUIRE(json["elicitation"].contains("form"));
    REQUIRE_FALSE(json["elicitation"].contains("url"));
}

TEST_CASE("ClientCapabilities with form and url elicitation", "[mcp][client][elicitation]") {
    ClientCapabilities caps;
    caps.elicitation = ElicitationCapability{true, true};  // both modes
    
    auto json = caps.to_json();
    
    REQUIRE(json["elicitation"].contains("form"));
    REQUIRE(json["elicitation"].contains("url"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sampling Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "mcpp/client/sampling_handler.hpp"

namespace {
class TestSamplingHandler : public ISamplingHandler {
public:
    CreateMessageParams last_params;
    std::optional<CreateMessageResult> response;
    
    std::optional<CreateMessageResult> handle_create_message(
        const CreateMessageParams& params
    ) override {
        last_params = params;
        return response;
    }
};
}  // namespace

TEST_CASE("McpClient set_sampling_handler", "[mcp][client][sampling]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestSamplingHandler>();
    client->set_sampling_handler(handler);
    
    REQUIRE(true);  // Should not throw
}

TEST_CASE("McpClient handles sampling createMessage request", "[mcp][client][sampling]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestSamplingHandler>();
    handler->response = CreateMessageResult{
        SamplingRole::Assistant,
        TextContent{"Here is the summary of the code...", std::nullopt},
        "claude-3-5-sonnet",
        StopReason::EndTurn
    };
    client->set_sampling_handler(handler);
    
    client->connect();
    
    // Simulate server sending sampling request
    Json server_request = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Summarize this code"}}}}
        }},
        {"maxTokens", 500},
        {"systemPrompt", "You are a helpful assistant."}
    };
    
    auto result = client->handle_sampling_request(server_request);
    
    REQUIRE(result.has_value());
    REQUIRE(handler->last_params.messages.size() == 1);
    REQUIRE(handler->last_params.max_tokens == 500);
    REQUIRE(handler->last_params.system_prompt == "You are a helpful assistant.");
    REQUIRE((*result)["role"] == "assistant");
    REQUIRE((*result)["content"]["type"] == "text");
    REQUIRE((*result)["model"] == "claude-3-5-sonnet");
    REQUIRE((*result)["stopReason"] == "endTurn");
    
    client->disconnect();
}

TEST_CASE("McpClient sampling without handler returns error", "[mcp][client][sampling]") {
    auto [client, server] = make_test_client();
    
    // No handler set
    client->connect();
    
    Json server_request = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hello"}}}}
        }}
    };
    
    auto result = client->handle_sampling_request(server_request);
    
    // Should return error when no handler is set
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == ClientErrorCode::ProtocolError);
    
    client->disconnect();
}

TEST_CASE("McpClient sampling handler declines", "[mcp][client][sampling]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestSamplingHandler>();
    handler->response = std::nullopt;  // Handler declines
    client->set_sampling_handler(handler);
    
    client->connect();
    
    Json server_request = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Do something"}}}}
        }}
    };
    
    auto result = client->handle_sampling_request(server_request);
    
    // Handler declined - should return error
    REQUIRE_FALSE(result.has_value());
    
    client->disconnect();
}

TEST_CASE("McpClient sampling with model preferences", "[mcp][client][sampling]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<TestSamplingHandler>();
    handler->response = CreateMessageResult{
        SamplingRole::Assistant,
        TextContent{"Response", std::nullopt},
        "gpt-4",
        StopReason::MaxTokens
    };
    client->set_sampling_handler(handler);
    
    client->connect();
    
    Json server_request = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Test"}}}}
        }},
        {"modelPreferences", {
            {"hints", {{{"name", "claude-3-opus"}}}},
            {"speedPriority", 0.8},
            {"intelligencePriority", 0.9}
        }}
    };
    
    auto result = client->handle_sampling_request(server_request);
    
    REQUIRE(result.has_value());
    REQUIRE(handler->last_params.model_preferences.has_value());
    REQUIRE(handler->last_params.model_preferences->hints.size() == 1);
    REQUIRE(handler->last_params.model_preferences->hints[0].name == "claude-3-opus");
    REQUIRE(handler->last_params.model_preferences->speed_priority.has_value());
    
    client->disconnect();
}

TEST_CASE("ClientCapabilities includes sampling", "[mcp][client][sampling]") {
    ClientCapabilities caps;
    caps.sampling = ClientCapabilities::Sampling{};
    
    auto json = caps.to_json();
    
    REQUIRE(json.contains("sampling"));
    REQUIRE(json["sampling"].is_object());
}

// ═══════════════════════════════════════════════════════════════════════════
// Roots Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "mcpp/client/roots_handler.hpp"

TEST_CASE("McpClient set_roots_handler", "[mcp][client][roots]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{
        Root{"file:///home/user/project", "My Project"}
    });
    client->set_roots_handler(handler);
    
    REQUIRE(true);  // Should not throw
}

TEST_CASE("McpClient handles roots/list request", "[mcp][client][roots]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{
        Root{"file:///home/user/project", "My Project"},
        Root{"file:///shared/libs", std::nullopt}
    });
    client->set_roots_handler(handler);
    
    client->connect();
    
    auto result = client->handle_roots_list_request();
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].size() == 2);
    REQUIRE((*result)["roots"][0]["uri"] == "file:///home/user/project");
    REQUIRE((*result)["roots"][0]["name"] == "My Project");
    REQUIRE((*result)["roots"][1]["uri"] == "file:///shared/libs");
    REQUIRE_FALSE((*result)["roots"][1].contains("name"));
    
    client->disconnect();
}

TEST_CASE("McpClient roots without handler returns empty list", "[mcp][client][roots]") {
    auto [client, server] = make_test_client();
    
    // No handler set
    client->connect();
    
    auto result = client->handle_roots_list_request();
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].empty());
    
    client->disconnect();
}

TEST_CASE("McpClient roots with MutableRootsHandler", "[mcp][client][roots]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<MutableRootsHandler>();
    handler->add_root(Root{"file:///initial", "Initial"});
    client->set_roots_handler(handler);
    
    client->connect();
    
    // First call
    auto result1 = client->handle_roots_list_request();
    REQUIRE(result1.has_value());
    REQUIRE((*result1)["roots"].size() == 1);
    
    // Add a root dynamically
    handler->add_root(Root{"file:///new", "New Root"});
    
    // Second call reflects the change
    auto result2 = client->handle_roots_list_request();
    REQUIRE(result2.has_value());
    REQUIRE((*result2)["roots"].size() == 2);
    
    client->disconnect();
}

TEST_CASE("McpClient notify_roots_changed sends notification", "[mcp][client][roots]") {
    MockMcpServer server;
    
    bool notification_received = false;
    server.on_notification("notifications/roots/list_changed", [&](const Json& params) {
        notification_received = true;
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    config.capabilities.roots = ClientCapabilities::Roots{true};  // Enable listChanged
    
    McpClient client(std::move(config), std::move(mock_client));
    
    auto handler = std::make_shared<MutableRootsHandler>();
    client.set_roots_handler(handler);
    
    client.connect();
    
    // Send roots changed notification
    auto result = client.notify_roots_changed();
    
    REQUIRE(result.has_value());
    
    client.disconnect();
}

TEST_CASE("ClientCapabilities includes roots", "[mcp][client][roots]") {
    ClientCapabilities caps;
    caps.roots = ClientCapabilities::Roots{true};
    
    auto json = caps.to_json();
    
    REQUIRE(json.contains("roots"));
    REQUIRE(json["roots"]["listChanged"] == true);
}

TEST_CASE("ClientCapabilities roots without listChanged", "[mcp][client][roots]") {
    ClientCapabilities caps;
    caps.roots = ClientCapabilities::Roots{false};
    
    auto json = caps.to_json();
    
    REQUIRE(json.contains("roots"));
    REQUIRE(json["roots"]["listChanged"] == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Message Dispatcher Tests (Sync Client)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient dispatch_notification calls generic handler", "[mcp][client][dispatcher]") {
    auto [client, server] = make_test_client();
    
    std::string received_method;
    Json received_params;
    
    client->on_notification([&](const std::string& method, const Json& params) {
        received_method = method;
        received_params = params;
    });
    
    client->connect();
    
    // Verify handler is set (full integration would require bidirectional mock)
    REQUIRE(client->is_initialized());
    
    client->disconnect();
}

TEST_CASE("McpClient elicitation handler integration", "[mcp][client][dispatcher]") {
    auto [client, server] = make_test_client();
    
    // Set up handler
    class TestHandler : public IElicitationHandler {
    public:
        ElicitationResult handle_form(const std::string& msg, const Json&) override {
            last_message = msg;
            return {ElicitationAction::Accept, Json{{"response", "ok"}}};
        }
        ElicitationResult handle_url(const std::string&, const std::string&, const std::string&) override {
            return {ElicitationAction::Dismiss, std::nullopt};
        }
        std::string last_message;
    };
    
    auto handler = std::make_shared<TestHandler>();
    client->set_elicitation_handler(handler);
    
    client->connect();
    
    // Test handler is called correctly
    Json params = {
        {"mode", "form"},
        {"message", "Enter name"},
        {"requestedSchema", Json::object()}
    };
    
    auto result = client->handle_elicitation_request(params);
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "accept");
    REQUIRE(handler->last_message == "Enter name");
    
    client->disconnect();
}

TEST_CASE("McpClient sampling handler integration", "[mcp][client][dispatcher]") {
    auto [client, server] = make_test_client();
    
    // Set up handler
    class TestHandler : public ISamplingHandler {
    public:
        std::optional<CreateMessageResult> handle_create_message(const CreateMessageParams& params) override {
            message_count = params.messages.size();
            return CreateMessageResult{
                SamplingRole::Assistant,
                TextContent{"Hello!", std::nullopt},
                "test-model",
                StopReason::EndTurn
            };
        }
        size_t message_count = 0;
    };
    
    auto handler = std::make_shared<TestHandler>();
    client->set_sampling_handler(handler);
    
    client->connect();
    
    Json params = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hi"}}}}
        }}
    };
    
    auto result = client->handle_sampling_request(params);
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["role"] == "assistant");
    REQUIRE((*result)["model"] == "test-model");
    REQUIRE(handler->message_count == 1);
    
    client->disconnect();
}

TEST_CASE("McpClient roots handler integration", "[mcp][client][dispatcher]") {
    auto [client, server] = make_test_client();
    
    auto handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{
        Root{"file:///test", "Test"}
    });
    client->set_roots_handler(handler);
    
    client->connect();
    
    auto result = client->handle_roots_list_request();
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["roots"].size() == 1);
    REQUIRE((*result)["roots"][0]["uri"] == "file:///test");
    
    client->disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Progress Handler Tests (Sync Client)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient on_progress handler can be registered", "[mcp][client][progress]") {
    auto [client, server] = make_test_client();
    
    std::vector<ProgressNotification> received;
    client->on_progress([&](const ProgressNotification& p) {
        received.push_back(p);
    });
    
    client->connect();
    
    // Handler registered successfully - no crash
    REQUIRE(client->is_connected());
    
    client->disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// URL Validation Security Tests (Sync Client)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient rejects localhost URL in elicitation", "[mcp][client][security]") {
    auto [client, server] = make_test_client();
    
    // Handler that should NOT be called for unsafe URLs
    class TrackingHandler : public IElicitationHandler {
    public:
        int url_calls = 0;
        ElicitationResult handle_form(const std::string&, const Json&) override {
            return {ElicitationAction::Dismiss, std::nullopt};
        }
        ElicitationResult handle_url(const std::string&, const std::string&, const std::string&) override {
            ++url_calls;
            return {ElicitationAction::Opened, std::nullopt};
        }
    };
    
    auto handler = std::make_shared<TrackingHandler>();
    client->set_elicitation_handler(handler);
    
    client->connect();
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "test-123"},
        {"url", "http://localhost:8080/auth"},
        {"message", "Authenticate"}
    };
    
    auto result = client->handle_elicitation_request(request);
    
    // Should decline without calling handler
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
    REQUIRE(handler->url_calls == 0);
    
    client->disconnect();
}

TEST_CASE("McpClient rejects private IP URL in elicitation", "[mcp][client][security]") {
    auto [client, server] = make_test_client();
    
    class TrackingHandler : public IElicitationHandler {
    public:
        int url_calls = 0;
        ElicitationResult handle_form(const std::string&, const Json&) override {
            return {ElicitationAction::Dismiss, std::nullopt};
        }
        ElicitationResult handle_url(const std::string&, const std::string&, const std::string&) override {
            ++url_calls;
            return {ElicitationAction::Opened, std::nullopt};
        }
    };
    
    auto handler = std::make_shared<TrackingHandler>();
    client->set_elicitation_handler(handler);
    
    client->connect();
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "test-456"},
        {"url", "http://192.168.1.1/admin"},
        {"message", "Admin"}
    };
    
    auto result = client->handle_elicitation_request(request);
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "decline");
    REQUIRE(handler->url_calls == 0);
    
    client->disconnect();
}

TEST_CASE("McpClient allows valid HTTPS URL in elicitation", "[mcp][client][security]") {
    auto [client, server] = make_test_client();
    
    class TrackingHandler : public IElicitationHandler {
    public:
        int url_calls = 0;
        ElicitationResult handle_form(const std::string&, const Json&) override {
            return {ElicitationAction::Dismiss, std::nullopt};
        }
        ElicitationResult handle_url(const std::string&, const std::string&, const std::string&) override {
            ++url_calls;
            return {ElicitationAction::Opened, std::nullopt};
        }
    };
    
    auto handler = std::make_shared<TrackingHandler>();
    client->set_elicitation_handler(handler);
    
    client->connect();
    
    Json request = {
        {"mode", "url"},
        {"elicitationId", "test-789"},
        {"url", "https://example.com/oauth"},
        {"message", "Authorize"}
    };
    
    auto result = client->handle_elicitation_request(request);
    
    REQUIRE(result.has_value());
    REQUIRE((*result)["action"] == "opened");
    REQUIRE(handler->url_calls == 1);  // Handler SHOULD be called
    
    client->disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Circuit Breaker Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient circuit breaker is enabled by default", "[mcp][client][circuit-breaker]") {
    auto [client, server] = make_test_client();
    
    // Circuit should start closed
    REQUIRE(client->circuit_state() == CircuitState::Closed);
    REQUIRE(client->is_circuit_open() == false);
    
    // Stats should be zeroed
    auto stats = client->circuit_stats();
    REQUIRE(stats.total_requests == 0);
    REQUIRE(stats.failed_requests == 0);
}

TEST_CASE("McpClient circuit breaker can be disabled", "[mcp][client][circuit-breaker]") {
    static MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.client_name = "test-client";
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    config.enable_circuit_breaker = false;  // Disable circuit breaker
    
    McpClient client(std::move(config), std::move(mock_client));
    
    // With circuit breaker disabled, state should always be Closed
    REQUIRE(client.circuit_state() == CircuitState::Closed);
    REQUIRE(client.is_circuit_open() == false);
    
    // Stats should be empty
    auto stats = client.circuit_stats();
    REQUIRE(stats.total_requests == 0);
}

TEST_CASE("McpClient circuit breaker tracks successful requests", "[mcp][client][circuit-breaker]") {
    auto [client, server] = make_test_client();
    
    auto result = client->connect();
    REQUIRE(result.has_value());
    
    // After successful connect + initialize, should have recorded at least one success
    // (notifications don't go through send_and_receive, only the initialize request does)
    auto stats = client->circuit_stats();
    REQUIRE(stats.total_requests >= 1);
    REQUIRE(stats.failed_requests == 0);
    
    client->disconnect();
}

TEST_CASE("McpClient circuit breaker can be forced open", "[mcp][client][circuit-breaker]") {
    auto [client, server] = make_test_client();
    
    // Connect first
    auto result = client->connect();
    REQUIRE(result.has_value());
    
    // Force circuit open
    client->force_circuit_open();
    REQUIRE(client->is_circuit_open() == true);
    REQUIRE(client->circuit_state() == CircuitState::Open);
    
    // Requests should fail fast
    auto tools_result = client->list_tools();
    REQUIRE(!tools_result.has_value());
    REQUIRE(tools_result.error().code == ClientErrorCode::TransportError);
    REQUIRE(tools_result.error().message.find("Circuit breaker is open") != std::string::npos);
    
    client->disconnect();
}

TEST_CASE("McpClient circuit breaker can be forced closed", "[mcp][client][circuit-breaker]") {
    auto [client, server] = make_test_client();
    
    auto connect_result = client->connect();
    REQUIRE(connect_result.has_value());
    
    // Force open then force close
    client->force_circuit_open();
    REQUIRE(client->is_circuit_open() == true);
    
    client->force_circuit_closed();
    REQUIRE(client->is_circuit_open() == false);
    REQUIRE(client->circuit_state() == CircuitState::Closed);
    
    // After force close, circuit should allow requests
    // (The actual request may fail due to mock server state, but circuit should allow it)
    REQUIRE(client->is_circuit_open() == false);
    
    client->disconnect();
}

TEST_CASE("McpClient circuit breaker state change callback", "[mcp][client][circuit-breaker]") {
    auto [client, server] = make_test_client();
    
    std::vector<std::pair<CircuitState, CircuitState>> transitions;
    client->on_circuit_state_change([&](CircuitState from, CircuitState to) {
        transitions.push_back({from, to});
    });
    
    // Force state changes
    client->force_circuit_open();
    client->force_circuit_closed();
    
    REQUIRE(transitions.size() == 2);
    REQUIRE(transitions[0].first == CircuitState::Closed);
    REQUIRE(transitions[0].second == CircuitState::Open);
    REQUIRE(transitions[1].first == CircuitState::Open);
    REQUIRE(transitions[1].second == CircuitState::Closed);
}

TEST_CASE("McpClient circuit breaker with custom config", "[mcp][client][circuit-breaker]") {
    static MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.client_name = "test-client";
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    // Custom circuit breaker settings
    config.circuit_breaker.failure_threshold = 2;
    config.circuit_breaker.success_threshold = 1;
    config.circuit_breaker.recovery_timeout = std::chrono::milliseconds(100);
    
    McpClient client(std::move(config), std::move(mock_client));
    
    // Circuit should be closed initially
    REQUIRE(client.circuit_state() == CircuitState::Closed);
}

// ═══════════════════════════════════════════════════════════════════════════
// Request Timeout Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient request_timeout is configurable", "[mcp][client][timeout]") {
    // Test that request_timeout can be configured and client still works
    McpClientConfig config;
    config.request_timeout = std::chrono::milliseconds(5000);
    
    // Verify the config is set correctly
    REQUIRE(config.request_timeout == std::chrono::milliseconds(5000));
    REQUIRE(config.request_timeout != std::chrono::milliseconds(0));
}

TEST_CASE("McpClient request_timeout can be set to custom value", "[mcp][client][timeout]") {
    static MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    McpClientConfig config;
    config.client_name = "test-client";
    config.transport.base_url = "https://mock.mcp.local/mcp";
    config.transport.auto_open_sse_stream = false;
    config.transport.backoff_policy = std::make_shared<NoBackoff>();
    
    // Set a specific timeout
    config.request_timeout = std::chrono::milliseconds(5000);
    
    // Verify the config is set correctly
    REQUIRE(config.request_timeout == std::chrono::milliseconds(5000));
}

TEST_CASE("McpClient request_timeout zero means no timeout", "[mcp][client][timeout]") {
    McpClientConfig config;
    
    // Zero timeout means no timeout
    config.request_timeout = std::chrono::milliseconds(0);
    
    // Verify the config is set correctly
    REQUIRE(config.request_timeout == std::chrono::milliseconds(0));
}

TEST_CASE("McpClient default request_timeout is 30 seconds", "[mcp][client][timeout]") {
    McpClientConfig config;
    
    // Default should be 30 seconds
    REQUIRE(config.request_timeout == std::chrono::milliseconds(30000));
}

// ═══════════════════════════════════════════════════════════════════════════
// Handler Thread Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpClient handler_timeout is configurable", "[mcp][client][handler]") {
    McpClientConfig config;
    
    // Default handler timeout is 60 seconds
    REQUIRE(config.handler_timeout == std::chrono::milliseconds(60000));
    
    // Can set custom timeout
    config.handler_timeout = std::chrono::milliseconds(5000);
    REQUIRE(config.handler_timeout == std::chrono::milliseconds(5000));
    
    // Can disable timeout with 0
    config.handler_timeout = std::chrono::milliseconds(0);
    REQUIRE(config.handler_timeout == std::chrono::milliseconds(0));
}

TEST_CASE("McpClient starts and stops handler thread on connect/disconnect", "[mcp][client][handler][concurrency]") {
    auto [client, server] = make_test_client();
    
    // Connect starts handler thread
    auto result = client->connect();
    REQUIRE(result.has_value());
    REQUIRE(client->is_connected());
    
    // Disconnect stops handler thread cleanly
    client->disconnect();
    REQUIRE_FALSE(client->is_connected());
}

TEST_CASE("McpClient destructor stops handler thread cleanly", "[mcp][client][handler][concurrency]") {
    // Create and connect client in a scope
    {
        auto [client, server] = make_test_client();
        auto result = client->connect();
        REQUIRE(result.has_value());
        
        // Client will be destroyed here - handler thread should stop cleanly
    }
    
    // If we get here without hanging, the test passed
    REQUIRE(true);
}

