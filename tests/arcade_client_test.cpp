// ─────────────────────────────────────────────────────────────────────────────
// Arcade Client Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for the Arcade AI gateway client factory and configuration.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "mcpp/client/arcade_client.hpp"
#include "mocks/mock_http_client.hpp"

using namespace mcpp;
using namespace mcpp::testing;
using namespace Catch::Matchers;

// ═══════════════════════════════════════════════════════════════════════════
// ArcadeConfig Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ArcadeConfig validation", "[arcade][config]") {
    SECTION("valid config") {
        ArcadeConfig config;
        config.gateway_slug = "my-gateway";
        config.api_key = "arc_test_key";
        config.user_id = "user@example.com";
        
        REQUIRE(config.is_valid());
        REQUIRE(config.validation_error().empty());
    }
    
    SECTION("missing gateway_slug") {
        ArcadeConfig config;
        config.api_key = "arc_test_key";
        config.user_id = "user@example.com";
        
        REQUIRE_FALSE(config.is_valid());
        REQUIRE_THAT(config.validation_error(), ContainsSubstring("Gateway slug"));
    }
    
    SECTION("missing api_key") {
        ArcadeConfig config;
        config.gateway_slug = "my-gateway";
        config.user_id = "user@example.com";
        
        REQUIRE_FALSE(config.is_valid());
        REQUIRE_THAT(config.validation_error(), ContainsSubstring("API key"));
    }
    
    SECTION("missing user_id") {
        ArcadeConfig config;
        config.gateway_slug = "my-gateway";
        config.api_key = "arc_test_key";
        
        REQUIRE_FALSE(config.is_valid());
        REQUIRE_THAT(config.validation_error(), ContainsSubstring("User ID"));
    }
}

TEST_CASE("ArcadeConfig URL building", "[arcade][config]") {
    SECTION("default base URL") {
        ArcadeConfig config;
        config.gateway_slug = "ultracoolserver";
        
        REQUIRE(config.build_url() == "https://api.arcade.dev/mcp/ultracoolserver");
    }
    
    SECTION("custom base URL") {
        ArcadeConfig config;
        config.gateway_slug = "my-gateway";
        config.base_url = "https://custom.arcade.dev";
        
        REQUIRE(config.build_url() == "https://custom.arcade.dev/mcp/my-gateway");
    }
    
    SECTION("base URL with trailing slash") {
        ArcadeConfig config;
        config.gateway_slug = "test";
        config.base_url = "https://api.arcade.dev/";
        
        REQUIRE(config.build_url() == "https://api.arcade.dev/mcp/test");
    }
}

TEST_CASE("ArcadeConfig builder methods", "[arcade][config][builder]") {
    ArcadeConfig config;
    
    config.with_gateway("ultracoolserver")
          .with_api_key("arc_xxx")
          .with_user_id("francisco@arcade.dev")
          .with_connect_timeout(std::chrono::milliseconds{5000})
          .with_read_timeout(std::chrono::milliseconds{15000})
          .with_max_retries(5)
          .with_circuit_breaker(false);
    
    REQUIRE(config.gateway_slug == "ultracoolserver");
    REQUIRE(config.api_key == "arc_xxx");
    REQUIRE(config.user_id == "francisco@arcade.dev");
    REQUIRE(config.connect_timeout == std::chrono::milliseconds{5000});
    REQUIRE(config.read_timeout == std::chrono::milliseconds{15000});
    REQUIRE(config.max_retries == 5);
    REQUIRE(config.enable_circuit_breaker == false);
    REQUIRE(config.is_valid());
}

// ═══════════════════════════════════════════════════════════════════════════
// Configuration Conversion Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("to_mcp_config converts ArcadeConfig correctly", "[arcade][config]") {
    ArcadeConfig arcade_config;
    arcade_config.gateway_slug = "ultracoolserver";
    arcade_config.api_key = "arc_test_token";
    arcade_config.user_id = "test@example.com";
    arcade_config.client_name = "test-client";
    arcade_config.client_version = "2.0.0";
    arcade_config.connect_timeout = std::chrono::milliseconds{5000};
    arcade_config.read_timeout = std::chrono::milliseconds{20000};
    arcade_config.request_timeout = std::chrono::milliseconds{45000};
    arcade_config.max_retries = 5;
    arcade_config.enable_circuit_breaker = false;
    
    auto mcp_config = arcade::to_mcp_config(arcade_config);
    
    // Client info
    REQUIRE(mcp_config.client_name == "test-client");
    REQUIRE(mcp_config.client_version == "2.0.0");
    
    // Transport URL
    REQUIRE(mcp_config.transport.base_url == "https://api.arcade.dev/mcp/ultracoolserver");
    
    // Transport timeouts
    REQUIRE(mcp_config.transport.connect_timeout == std::chrono::milliseconds{5000});
    REQUIRE(mcp_config.transport.read_timeout == std::chrono::milliseconds{20000});
    REQUIRE(mcp_config.transport.max_retries == 5);
    
    // Headers
    REQUIRE(mcp_config.transport.default_headers.count("Authorization") == 1);
    REQUIRE(mcp_config.transport.default_headers.at("Authorization") == "Bearer arc_test_token");
    REQUIRE(mcp_config.transport.default_headers.count("Arcade-User-ID") == 1);
    REQUIRE(mcp_config.transport.default_headers.at("Arcade-User-ID") == "test@example.com");
    
    // SSE should be disabled
    REQUIRE(mcp_config.transport.auto_open_sse_stream == false);
    
    // Request timeout
    REQUIRE(mcp_config.request_timeout == std::chrono::milliseconds{45000});
    
    // Circuit breaker
    REQUIRE(mcp_config.enable_circuit_breaker == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Factory Function Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("arcade::create_client throws on invalid config", "[arcade][factory]") {
    ArcadeConfig invalid_config;
    // Missing all required fields
    
    REQUIRE_THROWS_AS(arcade::create_client(invalid_config), std::invalid_argument);
}

TEST_CASE("arcade::create_client creates client with valid config", "[arcade][factory]") {
    ArcadeConfig config;
    config.gateway_slug = "test-gateway";
    config.api_key = "arc_xxx";
    config.user_id = "user@test.com";
    
    auto client = arcade::create_client(config);
    
    REQUIRE(client != nullptr);
    REQUIRE_FALSE(client->is_connected());
}

TEST_CASE("arcade::connect returns error on invalid config", "[arcade][factory]") {
    ArcadeConfig invalid_config;
    // Missing all required fields
    
    auto result = arcade::connect(invalid_config);
    
    REQUIRE_FALSE(result.has_value());
    REQUIRE_THAT(result.error().message, ContainsSubstring("Invalid"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Integration Tests with Mock
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Arcade client sends correct headers", "[arcade][integration]") {
    ArcadeConfig config;
    config.gateway_slug = "ultracoolserver";
    config.api_key = "arc_test_api_key_12345";
    config.user_id = "francisco@arcade.dev";
    
    auto mcp_config = arcade::to_mcp_config(config);
    
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    // Queue initialize response
    mock_raw->queue_json_response(200, R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "protocolVersion": "2025-06-18",
            "serverInfo": {"name": "ultracoolserver", "version": "1.0.0"},
            "capabilities": {"tools": {}}
        }
    })");
    
    // Queue initialized notification response (202 Accepted)
    mock_raw->queue_response(202, "");
    
    McpClient client(mcp_config, std::move(mock));
    auto connect_result = client.connect();
    
    // Verify headers were set
    auto headers = mock_raw->default_headers();
    REQUIRE(headers.count("Authorization") == 1);
    REQUIRE(headers.at("Authorization") == "Bearer arc_test_api_key_12345");
    REQUIRE(headers.count("Arcade-User-ID") == 1);
    REQUIRE(headers.at("Arcade-User-ID") == "francisco@arcade.dev");
    
    client.disconnect();
}

TEST_CASE("Arcade client connects and initializes", "[arcade][integration]") {
    ArcadeConfig config;
    config.gateway_slug = "ultracoolserver";
    config.api_key = "arc_xxx";
    config.user_id = "user@example.com";
    
    auto mcp_config = arcade::to_mcp_config(config);
    
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    // Queue initialize response
    mock_raw->queue_json_response(200, R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "protocolVersion": "2025-06-18",
            "serverInfo": {"name": "ultracoolserver", "version": "1.0.0"},
            "capabilities": {"tools": {}},
            "instructions": "Github, Linear, Slack"
        }
    })");
    
    // Queue initialized notification response
    mock_raw->queue_response(202, "");
    
    McpClient client(mcp_config, std::move(mock));
    auto connect_result = client.connect();
    
    REQUIRE(connect_result.has_value());
    REQUIRE(connect_result->server_info.name == "ultracoolserver");
    REQUIRE(connect_result->server_info.version == "1.0.0");
    REQUIRE(connect_result->instructions.has_value());
    REQUIRE(connect_result->instructions.value() == "Github, Linear, Slack");
    
    REQUIRE(client.is_connected());
    REQUIRE(client.is_initialized());
    
    client.disconnect();
}

TEST_CASE("Arcade client lists tools", "[arcade][integration]") {
    ArcadeConfig config;
    config.gateway_slug = "ultracoolserver";
    config.api_key = "arc_xxx";
    config.user_id = "user@example.com";
    
    auto mcp_config = arcade::to_mcp_config(config);
    
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    // Queue initialize response
    mock_raw->queue_json_response(200, R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "protocolVersion": "2025-06-18",
            "serverInfo": {"name": "ultracoolserver", "version": "1.0.0"},
            "capabilities": {"tools": {}}
        }
    })");
    mock_raw->queue_response(202, "");
    
    // Queue tools/list response
    mock_raw->queue_json_response(200, R"({
        "jsonrpc": "2.0",
        "id": 2,
        "result": {
            "tools": [
                {"name": "Github_WhoAmI", "description": "Get authenticated user info"},
                {"name": "Github_SearchMyRepos", "description": "Search repositories"},
                {"name": "Slack_SendMessage", "description": "Send a Slack message"}
            ]
        }
    })");
    
    McpClient client(mcp_config, std::move(mock));
    auto connect_result = client.connect();
    REQUIRE(connect_result.has_value());
    
    auto tools_result = client.list_tools();
    
    REQUIRE(tools_result.has_value());
    REQUIRE(tools_result->tools.size() == 3);
    REQUIRE(tools_result->tools[0].name == "Github_WhoAmI");
    REQUIRE(tools_result->tools[1].name == "Github_SearchMyRepos");
    REQUIRE(tools_result->tools[2].name == "Slack_SendMessage");
    
    client.disconnect();
}

TEST_CASE("Arcade client calls tool", "[arcade][integration]") {
    ArcadeConfig config;
    config.gateway_slug = "ultracoolserver";
    config.api_key = "arc_xxx";
    config.user_id = "user@example.com";
    
    auto mcp_config = arcade::to_mcp_config(config);
    
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    // Queue initialize
    mock_raw->queue_json_response(200, R"({
        "jsonrpc": "2.0", "id": 1,
        "result": {
            "protocolVersion": "2025-06-18",
            "serverInfo": {"name": "test", "version": "1.0.0"},
            "capabilities": {"tools": {}}
        }
    })");
    mock_raw->queue_response(202, "");
    
    // Queue tool call response
    mock_raw->queue_json_response(200, R"({
        "jsonrpc": "2.0",
        "id": 2,
        "result": {
            "content": [
                {"type": "text", "text": "{\"login\": \"jottakka\", \"id\": 203343514}"}
            ]
        }
    })");
    
    McpClient client(mcp_config, std::move(mock));
    auto connect_result = client.connect();
    REQUIRE(connect_result.has_value());
    
    auto result = client.call_tool("Github_WhoAmI", {});
    
    REQUIRE(result.has_value());
    REQUIRE(result->content.size() == 1);
    REQUIRE(std::holds_alternative<TextContent>(result->content[0]));
    
    auto& text = std::get<TextContent>(result->content[0]);
    REQUIRE_THAT(text.text, ContainsSubstring("jottakka"));
    
    client.disconnect();
}

// ═══════════════════════════════════════════════════════════════════════════
// Environment Variable Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("config_from_env reads gateway from parameter", "[arcade][env]") {
    // Note: This test doesn't set env vars, so api_key and user_id will be empty
    auto config = arcade::config_from_env("my-gateway");
    
    REQUIRE(config.gateway_slug == "my-gateway");
    // api_key and user_id will be empty unless env vars are set
}

