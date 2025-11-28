#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

#include "mcpp/transport.hpp"

TEST_CASE("HttpClientConfig::from_url applies protocol friendly defaults", "[transport][http]") {
    auto config = mcpp::HttpClientConfig::from_url("https://api.example.com/mcp/");

    REQUIRE(config.base_url == "https://api.example.com/mcp");
    REQUIRE(config.handshake_endpoint == "/.well-known/mcp/handshake");
    REQUIRE(config.handshake_timeout == std::chrono::milliseconds{10'000});

    REQUIRE(config.default_headers.contains("Accept"));
    REQUIRE(config.default_headers.contains("Content-Type"));
    REQUIRE(config.default_headers.at("Accept") == "text/event-stream");
    REQUIRE(config.default_headers.at("Content-Type") == "application/json");
}

TEST_CASE("HttpClientConfig::with_bearer_token decorates headers without mutation", "[transport][http]") {
    auto base = mcpp::HttpClientConfig::from_url("https://api.example.com");
    auto secured = base.with_bearer_token("abc123");

    REQUIRE_FALSE(base.default_headers.contains("Authorization"));
    REQUIRE(secured.default_headers.at("Authorization") == "Bearer abc123");
}

TEST_CASE("HttpTransport handshake body advertises capabilities", "[transport][http]") {
    mcpp::HttpTransport transport{mcpp::HttpClientConfig::from_url("https://mcp.local")};

    const std::array<std::string_view, 3> capabilities{
        "resources", "prompts", "tools"
    };

    auto body = transport.build_handshake_body("mcpp-client", capabilities, "2024-11-21");

    REQUIRE(body["client"]["name"] == "mcpp-client");
    REQUIRE(body["protocol"]["version"] == "2024-11-21");

    auto advertised = body["capabilities"].get<std::vector<std::string>>();
    REQUIRE(advertised.size() == capabilities.size());
    REQUIRE(advertised.front() == "resources");
    REQUIRE(advertised.back() == "tools");
}


