// ─────────────────────────────────────────────────────────────────────────────
// Integration Tests - End-to-End MCP Protocol Tests
// ─────────────────────────────────────────────────────────────────────────────
// These tests verify the full MCP protocol flow using a mock server.
// They test the interaction between:
// - HttpTransport
// - Session management
// - JSON-RPC message handling
// - Error handling and retries

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/transport/http_transport.hpp"
#include "mcpp/transport/backoff_policy.hpp"
#include "mocks/mock_mcp_server.hpp"

#include <chrono>
#include <thread>

using namespace mcpp;
using namespace mcpp::testing;
using Json = nlohmann::json;
using namespace std::chrono_literals;

// ═══════════════════════════════════════════════════════════════════════════
// Test Helpers
// ═══════════════════════════════════════════════════════════════════════════

std::pair<std::unique_ptr<HttpTransport>, MockMcpServer*>
make_integration_transport() {
    static MockMcpServer server;  // Static to persist across helper calls
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();  // Fast tests
    
    auto transport = std::make_unique<HttpTransport>(
        std::move(config),
        std::move(mock_client)
    );
    
    return {std::move(transport), &server};
}

// ═══════════════════════════════════════════════════════════════════════════
// MCP Initialize Handshake Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MCP initialize handshake completes successfully", "[integration][mcp][handshake]") {
    MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    // Send initialize request
    Json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", Json::object()},
            {"clientInfo", {
                {"name", "mcpp-test"},
                {"version", "0.1.0"}
            }}
        }}
    };
    
    auto result = transport.send(init_request);
    REQUIRE(result.has_value());
    
    // Verify session was established
    REQUIRE(transport.session_id().has_value());
    REQUIRE(transport.session_id()->find("mock-session") != std::string::npos);
    
    // Receive response
    auto response = transport.receive();
    REQUIRE(response.has_value());
    REQUIRE(response->contains("result"));
    REQUIRE((*response)["result"]["protocolVersion"] == "2024-11-05");
    REQUIRE((*response)["result"]["serverInfo"]["name"] == "MockMcpServer");
    
    transport.stop();
}

TEST_CASE("MCP initialized notification is sent after initialize", "[integration][mcp][handshake]") {
    MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    // Initialize
    Json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    };
    transport.send(init_request);
    transport.receive();  // Consume response
    
    // Send initialized notification
    Json initialized = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    auto result = transport.send(initialized);
    REQUIRE(result.has_value());
    
    // Server should now be in initialized state
    REQUIRE(server.is_initialized());
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Tools API Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MCP tools/list returns available tools", "[integration][mcp][tools]") {
    MockMcpServer server;
    
    // Register tools
    server.on_request("tools/list", [](const Json& params) {
        return Json{
            {"tools", Json::array({
                {
                    {"name", "echo"},
                    {"description", "Echoes input back"},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"message", {{"type", "string"}}}
                        }}
                    }}
                },
                {
                    {"name", "add"},
                    {"description", "Adds two numbers"},
                    {"inputSchema", {
                        {"type", "object"},
                        {"properties", {
                            {"a", {{"type", "number"}}},
                            {"b", {{"type", "number"}}}
                        }}
                    }}
                }
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    // List tools
    Json request = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"}
    };
    transport.send(request);
    
    auto response = transport.receive();
    REQUIRE(response.has_value());
    REQUIRE((*response)["result"]["tools"].size() == 2);
    REQUIRE((*response)["result"]["tools"][0]["name"] == "echo");
    REQUIRE((*response)["result"]["tools"][1]["name"] == "add");
    
    transport.stop();
}

TEST_CASE("MCP tools/call executes a tool", "[integration][mcp][tools]") {
    MockMcpServer server;
    
    server.on_request("tools/call", [](const Json& params) {
        const auto tool_name = params.value("name", "");
        const auto args = params.value("arguments", Json::object());
        
        if (tool_name == "echo") {
            return Json{
                {"content", Json::array({
                    {{"type", "text"}, {"text", args.value("message", "")}}
                })}
            };
        }
        
        if (tool_name == "add") {
            const int a = args.value("a", 0);
            const int b = args.value("b", 0);
            return Json{
                {"content", Json::array({
                    {{"type", "text"}, {"text", std::to_string(a + b)}}
                })}
            };
        }
        
        throw std::runtime_error("Unknown tool: " + tool_name);
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    SECTION("Echo tool returns input") {
        Json request = {
            {"jsonrpc", "2.0"},
            {"id", 3},
            {"method", "tools/call"},
            {"params", {
                {"name", "echo"},
                {"arguments", {{"message", "Hello, MCP!"}}}
            }}
        };
        transport.send(request);
        
        auto response = transport.receive();
        REQUIRE(response.has_value());
        REQUIRE((*response)["result"]["content"][0]["text"] == "Hello, MCP!");
    }
    
    SECTION("Add tool computes sum") {
        Json request = {
            {"jsonrpc", "2.0"},
            {"id", 4},
            {"method", "tools/call"},
            {"params", {
                {"name", "add"},
                {"arguments", {{"a", 5}, {"b", 7}}}
            }}
        };
        transport.send(request);
        
        auto response = transport.receive();
        REQUIRE(response.has_value());
        REQUIRE((*response)["result"]["content"][0]["text"] == "12");
    }
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Resources API Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MCP resources/list returns available resources", "[integration][mcp][resources]") {
    MockMcpServer server;
    
    server.on_request("resources/list", [](const Json& params) {
        return Json{
            {"resources", Json::array({
                {
                    {"uri", "file:///config.json"},
                    {"name", "Configuration"},
                    {"mimeType", "application/json"}
                },
                {
                    {"uri", "file:///readme.md"},
                    {"name", "README"},
                    {"mimeType", "text/markdown"}
                }
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    Json request = {
        {"jsonrpc", "2.0"},
        {"id", 5},
        {"method", "resources/list"}
    };
    transport.send(request);
    
    auto response = transport.receive();
    REQUIRE(response.has_value());
    REQUIRE((*response)["result"]["resources"].size() == 2);
    REQUIRE((*response)["result"]["resources"][0]["uri"] == "file:///config.json");
    
    transport.stop();
}

TEST_CASE("MCP resources/read returns resource content", "[integration][mcp][resources]") {
    MockMcpServer server;
    
    server.on_request("resources/read", [](const Json& params) {
        const auto uri = params.value("uri", "");
        
        if (uri == "file:///config.json") {
            return Json{
                {"contents", Json::array({
                    {
                        {"uri", uri},
                        {"mimeType", "application/json"},
                        {"text", R"({"debug": true})"}
                    }
                })}
            };
        }
        
        throw std::runtime_error("Resource not found: " + uri);
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    Json request = {
        {"jsonrpc", "2.0"},
        {"id", 6},
        {"method", "resources/read"},
        {"params", {{"uri", "file:///config.json"}}}
    };
    transport.send(request);
    
    auto response = transport.receive();
    REQUIRE(response.has_value());
    REQUIRE((*response)["result"]["contents"][0]["text"] == R"({"debug": true})");
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Prompts API Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MCP prompts/list returns available prompts", "[integration][mcp][prompts]") {
    MockMcpServer server;
    
    server.on_request("prompts/list", [](const Json& params) {
        return Json{
            {"prompts", Json::array({
                {
                    {"name", "code-review"},
                    {"description", "Review code for issues"},
                    {"arguments", Json::array({
                        {{"name", "language"}, {"required", true}}
                    })}
                }
            })}
        };
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    Json request = {
        {"jsonrpc", "2.0"},
        {"id", 7},
        {"method", "prompts/list"}
    };
    transport.send(request);
    
    auto response = transport.receive();
    REQUIRE(response.has_value());
    REQUIRE((*response)["result"]["prompts"].size() == 1);
    REQUIRE((*response)["result"]["prompts"][0]["name"] == "code-review");
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MCP method not found returns error", "[integration][mcp][error]") {
    MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    Json request = {
        {"jsonrpc", "2.0"},
        {"id", 99},
        {"method", "nonexistent/method"}
    };
    transport.send(request);
    
    auto response = transport.receive();
    REQUIRE(response.has_value());
    REQUIRE(response->contains("error"));
    REQUIRE((*response)["error"]["code"] == -32601);  // Method not found
    REQUIRE((*response)["error"]["message"].get<std::string>().find("nonexistent/method") != std::string::npos);
    
    transport.stop();
}

TEST_CASE("MCP tool handler exception returns error", "[integration][mcp][error]") {
    MockMcpServer server;
    
    server.on_request("tools/call", [](const Json& params) -> Json {
        throw std::runtime_error("Tool execution failed");
    });
    
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    transport.start();
    
    Json request = {
        {"jsonrpc", "2.0"},
        {"id", 100},
        {"method", "tools/call"},
        {"params", {{"name", "failing-tool"}}}
    };
    transport.send(request);
    
    auto response = transport.receive();
    REQUIRE(response.has_value());
    REQUIRE(response->contains("error"));
    REQUIRE((*response)["error"]["code"] == -32000);  // Server error
    REQUIRE((*response)["error"]["message"] == "Tool execution failed");
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Session Management Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Session state transitions through full lifecycle", "[integration][mcp][session]") {
    MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    
    std::vector<SessionState> states;
    transport.on_session_state_change([&](SessionState, SessionState new_state) {
        states.push_back(new_state);
    });
    
    // Initial state
    REQUIRE(transport.session_state() == SessionState::Disconnected);
    
    // Start -> Connecting
    REQUIRE(transport.start().has_value());
    REQUIRE(transport.session_state() == SessionState::Connecting);
    
    // Send initialize -> Connected
    Json init = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"protocolVersion", "2024-11-05"}}}
    };
    (void)transport.send(init);
    (void)transport.receive();
    
    REQUIRE(transport.session_state() == SessionState::Connected);
    REQUIRE(transport.session_id().has_value());
    
    // Stop -> Closing -> Disconnected
    transport.stop();
    
    // Verify state transitions
    REQUIRE(std::find(states.begin(), states.end(), SessionState::Connecting) != states.end());
    REQUIRE(std::find(states.begin(), states.end(), SessionState::Connected) != states.end());
}

// ═══════════════════════════════════════════════════════════════════════════
// HttpTransport Start/Stop Lifecycle Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Integration: HttpTransport start returns error when already running", "[integration][lifecycle]") {
    MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    
    // First start should succeed
    auto result1 = transport.start();
    REQUIRE(result1.has_value());
    REQUIRE(transport.is_running());
    
    // Second start should fail
    auto result2 = transport.start();
    REQUIRE(!result2.has_value());
    REQUIRE(transport.is_running());  // Still running
    
    transport.stop();
    REQUIRE(!transport.is_running());
    
    // After stop, start should succeed again
    auto result3 = transport.start();
    REQUIRE(result3.has_value());
    REQUIRE(transport.is_running());
    
    transport.stop();
}

TEST_CASE("Integration: HttpTransport start/stop cycle preserves functionality", "[integration][lifecycle]") {
    MockMcpServer server;
    auto mock_client = std::make_unique<MockMcpHttpClient>(server);
    auto* mock_ptr = mock_client.get();
    
    HttpTransportConfig config;
    config.base_url = "https://mock.mcp.local/mcp";
    config.auto_open_sse_stream = false;
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    HttpTransport transport(std::move(config), std::move(mock_client));
    
    // First cycle
    REQUIRE(transport.start().has_value());
    
    Json init = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {}}};
    auto send_result = transport.send(init);
    REQUIRE(send_result.has_value());
    
    auto recv_result = transport.receive();
    REQUIRE(recv_result.has_value());
    
    transport.stop();
    
    // Reset mock for second cycle
    mock_ptr->reset();
    
    // Second cycle should work identically
    REQUIRE(transport.start().has_value());
    
    Json init2 = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "initialize"}, {"params", {}}};
    auto send_result2 = transport.send(init2);
    REQUIRE(send_result2.has_value());
    
    auto recv_result2 = transport.receive();
    REQUIRE(recv_result2.has_value());
    
    transport.stop();
}


