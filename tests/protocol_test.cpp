#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
// We haven't created this yet, but this is TDD - we expect it to exist
#include "mcpp/protocol.hpp"

using json = nlohmann::json;

TEST_CASE("JSON-RPC 2.0 Request Construction", "[protocol]") {
    // Goal: Create a valid JSON-RPC 2.0 request
    // Spec: https://www.jsonrpc.org/specification
    
    SECTION("Should create a basic request with method and id") {
        // We want a class 'JsonRpcRequest'
        mcpp::JsonRpcRequest request("ping", 1);
        
        json j = request.to_json();
        
        REQUIRE(j["jsonrpc"] == "2.0");
        REQUIRE(j["method"] == "ping");
        REQUIRE(j["id"] == 1);
        // Params should be omitted if empty, or empty object/array
        REQUIRE_FALSE(j.contains("params")); 
    }

    SECTION("Should create a notification (no id)") {
        mcpp::JsonRpcNotification notification("initialized");
        
        json j = notification.to_json();
        
        REQUIRE(j["jsonrpc"] == "2.0");
        REQUIRE(j["method"] == "initialized");
        REQUIRE_FALSE(j.contains("id"));
    }
}

