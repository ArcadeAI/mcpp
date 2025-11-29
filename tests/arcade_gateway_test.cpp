// ─────────────────────────────────────────────────────────────────────────────
// Arcade Gateway Integration Tests
// ─────────────────────────────────────────────────────────────────────────────
// Real integration tests against Arcade AI gateways.
// These tests require network access and valid Arcade credentials.
//
// To run:
//   export ARCADE_API_KEY="arc_xxx"
//   export ARCADE_USER_ID="user@example.com"
//   ./mcpp_tests "[arcade-gateway]"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "mcpp/client/arcade_client.hpp"

#include <cstdlib>

using namespace mcpp;
using namespace Catch::Matchers;

namespace {

// Check if Arcade credentials are available
bool has_arcade_credentials() {
    const char* api_key = std::getenv("ARCADE_API_KEY");
    const char* user_id = std::getenv("ARCADE_USER_ID");
    return api_key != nullptr && user_id != nullptr;
}

// Get Arcade config from environment
ArcadeConfig get_test_config(const std::string& gateway = "ultracoolserver") {
    return arcade::config_from_env(gateway);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Real Gateway Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Arcade gateway connect and get server info", "[arcade-gateway][integration]") {
    if (!has_arcade_credentials()) {
        SKIP("ARCADE_API_KEY and ARCADE_USER_ID environment variables required");
    }
    
    auto config = get_test_config();
    REQUIRE(config.is_valid());
    
    auto result = arcade::connect(config);
    
    REQUIRE(result.has_value());
    auto& [client, init_result] = *result;
    
    // Check server info
    REQUIRE(init_result.server_info.name == "ultracoolserver");
    REQUIRE_FALSE(init_result.server_info.version.empty());
    
    // Check capabilities
    REQUIRE(init_result.capabilities.tools.has_value());
    
    // Check instructions (should list available toolkits)
    REQUIRE(init_result.instructions.has_value());
    REQUIRE_THAT(init_result.instructions.value(), ContainsSubstring("Github"));
    
    client->disconnect();
}

TEST_CASE("Arcade gateway list tools", "[arcade-gateway][integration]") {
    if (!has_arcade_credentials()) {
        SKIP("ARCADE_API_KEY and ARCADE_USER_ID environment variables required");
    }
    
    auto config = get_test_config();
    auto result = arcade::connect(config);
    REQUIRE(result.has_value());
    auto& [client, _] = *result;
    
    auto tools_result = client->list_tools();
    
    REQUIRE(tools_result.has_value());
    REQUIRE_FALSE(tools_result->tools.empty());
    
    // Check for expected GitHub tools
    bool has_whoami = false;
    bool has_search = false;
    
    for (const auto& tool : tools_result->tools) {
        if (tool.name == "Github_WhoAmI") has_whoami = true;
        if (tool.name == "Github_SearchMyRepos") has_search = true;
    }
    
    REQUIRE(has_whoami);
    REQUIRE(has_search);
    
    client->disconnect();
}

TEST_CASE("Arcade gateway call Github_WhoAmI", "[arcade-gateway][integration]") {
    if (!has_arcade_credentials()) {
        SKIP("ARCADE_API_KEY and ARCADE_USER_ID environment variables required");
    }
    
    auto config = get_test_config();
    auto result = arcade::connect(config);
    REQUIRE(result.has_value());
    auto& [client, _] = *result;
    
    auto tool_result = client->call_tool("Github_WhoAmI", {});
    
    REQUIRE(tool_result.has_value());
    REQUIRE_FALSE(tool_result->content.empty());
    REQUIRE(std::holds_alternative<TextContent>(tool_result->content[0]));
    
    auto& text = std::get<TextContent>(tool_result->content[0]);
    
    // Response should contain user profile info
    REQUIRE_THAT(text.text, ContainsSubstring("profile"));
    REQUIRE_THAT(text.text, ContainsSubstring("login"));
    
    client->disconnect();
}

TEST_CASE("Arcade gateway call Github_SearchMyRepos", "[arcade-gateway][integration]") {
    if (!has_arcade_credentials()) {
        SKIP("ARCADE_API_KEY and ARCADE_USER_ID environment variables required");
    }
    
    auto config = get_test_config();
    auto result = arcade::connect(config);
    REQUIRE(result.has_value());
    auto& [client, _] = *result;
    
    auto tool_result = client->call_tool("Github_SearchMyRepos", {
        {"repo_name", "test"}
    });
    
    REQUIRE(tool_result.has_value());
    REQUIRE_FALSE(tool_result->content.empty());
    
    // Response should be JSON with search results
    auto& text = std::get<TextContent>(tool_result->content[0]);
    // Should contain either suggestions or matched (depends on results)
    bool has_expected = text.text.find("suggestions") != std::string::npos ||
                        text.text.find("matched") != std::string::npos;
    REQUIRE(has_expected);
    
    client->disconnect();
}

TEST_CASE("Arcade gateway handles tool errors gracefully", "[arcade-gateway][integration]") {
    if (!has_arcade_credentials()) {
        SKIP("ARCADE_API_KEY and ARCADE_USER_ID environment variables required");
    }
    
    auto config = get_test_config();
    auto result = arcade::connect(config);
    REQUIRE(result.has_value());
    auto& [client, _] = *result;
    
    // Call a tool with missing required parameter
    auto tool_result = client->call_tool("Github_GetIssue", {
        // Missing required parameters: owner, repo, issue_number
    });
    
    // Should fail with error
    REQUIRE_FALSE(tool_result.has_value());
    // Error message should indicate missing/required parameters
    bool has_error_info = tool_result.error().message.find("missing") != std::string::npos ||
                          tool_result.error().message.find("required") != std::string::npos;
    REQUIRE(has_error_info);
    
    client->disconnect();
}

TEST_CASE("Arcade gateway ping", "[arcade-gateway][integration]") {
    if (!has_arcade_credentials()) {
        SKIP("ARCADE_API_KEY and ARCADE_USER_ID environment variables required");
    }
    
    auto config = get_test_config();
    auto result = arcade::connect(config);
    REQUIRE(result.has_value());
    auto& [client, _] = *result;
    
    auto ping_result = client->ping();
    
    REQUIRE(ping_result.has_value());
    
    client->disconnect();
}

TEST_CASE("Arcade client reconnects after disconnect", "[arcade-gateway][integration]") {
    if (!has_arcade_credentials()) {
        SKIP("ARCADE_API_KEY and ARCADE_USER_ID environment variables required");
    }
    
    auto config = get_test_config();
    
    // First connection
    auto result1 = arcade::connect(config);
    REQUIRE(result1.has_value());
    auto& [client1, _1] = *result1;
    
    auto tools1 = client1->list_tools();
    REQUIRE(tools1.has_value());
    
    client1->disconnect();
    REQUIRE_FALSE(client1->is_connected());
    
    // Second connection (new client)
    auto result2 = arcade::connect(config);
    REQUIRE(result2.has_value());
    auto& [client2, _2] = *result2;
    
    auto tools2 = client2->list_tools();
    REQUIRE(tools2.has_value());
    REQUIRE(tools2->tools.size() == tools1->tools.size());
    
    client2->disconnect();
}

