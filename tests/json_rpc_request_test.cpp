#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/protocol/json_rpc.hpp"

using json = nlohmann::json;

TEST_CASE("JsonRpcRequest serializes ids and params deterministically", "[json-rpc][request]") {
    auto params = json::object({{"kind", "resources"}, {"limit", 25}});
    mcpp::JsonRpcRequest request{
        "resources/list",
        mcpp::JsonRpcId{std::int64_t{42}},
        params
    };

    auto j = request.to_json();

    REQUIRE(j["jsonrpc"] == "2.0");
    REQUIRE(j["method"] == "resources/list");
    REQUIRE(j["id"] == 42);
    REQUIRE(j["params"] == params);
}

TEST_CASE("JsonRpcRequest round-trips from JSON payloads", "[json-rpc][request]") {
    json payload = {
        {"jsonrpc", "2.0"},
        {"method", "session/handshake"},
        {"id", "req-001"},
        {"params", json::object({{"client", "mcpp"}, {"version", "0.1.0"}})}
    };

    auto parsed = mcpp::JsonRpcRequest::from_json(payload);
    REQUIRE(parsed.has_value());

    const auto& req = parsed.value();
    REQUIRE(req.method() == "session/handshake");

    auto id = req.id();
    REQUIRE(std::holds_alternative<std::string>(id.value));
    REQUIRE(std::get<std::string>(id.value) == "req-001");

    REQUIRE(req.params().has_value());
    REQUIRE(req.params()->at("client") == "mcpp");
}

TEST_CASE("JsonRpcNotification omits id and params when not provided", "[json-rpc][notification]") {
    mcpp::JsonRpcNotification notification{"session/ping"};
    auto j = notification.to_json();

    REQUIRE(j["jsonrpc"] == "2.0");
    REQUIRE(j["method"] == "session/ping");
    REQUIRE_FALSE(j.contains("id"));
    REQUIRE_FALSE(j.contains("params"));
}

TEST_CASE("JsonRpcRequest parsing surfaces detailed errors", "[json-rpc][request][error]") {
    json bad_version = {
        {"jsonrpc", "1.0"},
        {"method", "tools/list"},
        {"id", 1}
    };
    auto version_result = mcpp::JsonRpcRequest::from_json(bad_version);
    REQUIRE_FALSE(version_result.has_value());
    REQUIRE(version_result.error().code == mcpp::JsonError::Code::InvalidVersion);

    json missing_method = {
        {"jsonrpc", "2.0"},
        {"id", 2}
    };
    auto missing_method_result = mcpp::JsonRpcRequest::from_json(missing_method);
    REQUIRE_FALSE(missing_method_result.has_value());
    REQUIRE(missing_method_result.error().code == mcpp::JsonError::Code::MissingField);
}


