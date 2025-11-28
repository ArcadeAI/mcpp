#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mcpp/json/fast_json.hpp"

using namespace mcpp;
using Json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Basic Parsing Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("fast_parse handles simple objects", "[json][simdjson]") {
    auto result = fast_parse(R"({"key": "value"})");

    REQUIRE(result.has_value());
    REQUIRE(result->is_object());
    REQUIRE((*result)["key"] == "value");
}

TEST_CASE("fast_parse handles nested objects", "[json][simdjson]") {
    auto result = fast_parse(R"({
        "outer": {
            "inner": {
                "deep": "value"
            }
        }
    })");

    REQUIRE(result.has_value());
    REQUIRE((*result)["outer"]["inner"]["deep"] == "value");
}

TEST_CASE("fast_parse handles arrays", "[json][simdjson]") {
    auto result = fast_parse(R"([1, 2, 3, 4, 5])");

    REQUIRE(result.has_value());
    REQUIRE(result->is_array());
    REQUIRE(result->size() == 5);
    REQUIRE((*result)[0] == 1);
    REQUIRE((*result)[4] == 5);
}

TEST_CASE("fast_parse handles mixed arrays", "[json][simdjson]") {
    auto result = fast_parse(R"([1, "two", true, null, 3.14])");

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 5);
    REQUIRE((*result)[0] == 1);
    REQUIRE((*result)[1] == "two");
    REQUIRE((*result)[2] == true);
    REQUIRE((*result)[3].is_null());

    double val = (*result)[4].get<double>();
    REQUIRE_THAT(val, Catch::Matchers::WithinRel(3.14, 0.001));
}

TEST_CASE("fast_parse handles all JSON types", "[json][simdjson]") {
    auto result = fast_parse(R"({
        "string": "hello",
        "integer": 42,
        "negative": -17,
        "float": 3.14159,
        "bool_true": true,
        "bool_false": false,
        "null_value": null,
        "array": [1, 2, 3],
        "object": {"nested": "value"}
    })");

    REQUIRE(result.has_value());

    const auto& j = *result;
    REQUIRE(j["string"] == "hello");
    REQUIRE(j["integer"] == 42);
    REQUIRE(j["negative"] == -17);
    REQUIRE_THAT(j["float"].get<double>(), Catch::Matchers::WithinRel(3.14159, 0.00001));
    REQUIRE(j["bool_true"] == true);
    REQUIRE(j["bool_false"] == false);
    REQUIRE(j["null_value"].is_null());
    REQUIRE(j["array"].is_array());
    REQUIRE(j["object"]["nested"] == "value");
}

// ─────────────────────────────────────────────────────────────────────────────
// Number Handling Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("fast_parse handles large integers", "[json][simdjson]") {
    auto result = fast_parse(R"({"big": 9223372036854775807})");

    REQUIRE(result.has_value());
    REQUIRE((*result)["big"].get<std::int64_t>() == 9223372036854775807LL);
}

TEST_CASE("fast_parse handles unsigned integers", "[json][simdjson]") {
    auto result = fast_parse(R"({"unsigned": 18446744073709551615})");

    REQUIRE(result.has_value());
    REQUIRE((*result)["unsigned"].get<std::uint64_t>() == 18446744073709551615ULL);
}

TEST_CASE("fast_parse handles scientific notation", "[json][simdjson]") {
    auto result = fast_parse(R"({"sci": 1.23e10})");

    REQUIRE(result.has_value());
    REQUIRE_THAT((*result)["sci"].get<double>(), Catch::Matchers::WithinRel(1.23e10, 0.001));
}

// ─────────────────────────────────────────────────────────────────────────────
// String Handling Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("fast_parse handles escaped strings", "[json][simdjson]") {
    auto result = fast_parse(R"({"escaped": "line1\nline2\ttab"})");

    REQUIRE(result.has_value());
    REQUIRE((*result)["escaped"] == "line1\nline2\ttab");
}

TEST_CASE("fast_parse handles unicode strings", "[json][simdjson]") {
    auto result = fast_parse(R"({"unicode": "Hello \u4e16\u754c"})");

    REQUIRE(result.has_value());
    // \u4e16\u754c is "世界" (world in Chinese)
    REQUIRE((*result)["unicode"] == "Hello 世界");
}

TEST_CASE("fast_parse handles empty strings", "[json][simdjson]") {
    auto result = fast_parse(R"({"empty": ""})");

    REQUIRE(result.has_value());
    REQUIRE((*result)["empty"] == "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Error Handling Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("fast_parse returns error for invalid JSON", "[json][simdjson]") {
    // Use clearly malformed JSON - missing closing brace
    auto result = fast_parse(R"({"key": "value")");

    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().message.empty() == false);
}

TEST_CASE("fast_parse returns error for truncated JSON", "[json][simdjson]") {
    auto result = fast_parse(R"({"key": "val)");

    REQUIRE(result.has_value() == false);
}

TEST_CASE("fast_parse returns error for empty input", "[json][simdjson]") {
    auto result = fast_parse("");

    REQUIRE(result.has_value() == false);
}

TEST_CASE("fast_parse returns error for whitespace only", "[json][simdjson]") {
    auto result = fast_parse("   \n\t  ");

    REQUIRE(result.has_value() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge Cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("fast_parse handles empty object", "[json][simdjson]") {
    auto result = fast_parse("{}");

    REQUIRE(result.has_value());
    REQUIRE(result->is_object());
    REQUIRE(result->empty());
}

TEST_CASE("fast_parse handles empty array", "[json][simdjson]") {
    auto result = fast_parse("[]");

    REQUIRE(result.has_value());
    REQUIRE(result->is_array());
    REQUIRE(result->empty());
}

// Note: simdjson requires documents to be objects or arrays at the root level
// (per the JSON spec, standalone values are technically not valid JSON documents)
// This is different from nlohmann::json which accepts any JSON value
TEST_CASE("fast_parse requires object or array at root", "[json][simdjson]") {
    SECTION("standalone string is rejected") {
        auto result = fast_parse(R"("just a string")");
        // simdjson rejects bare strings as document root
        REQUIRE(result.has_value() == false);
    }

    SECTION("standalone number is rejected") {
        auto result = fast_parse("42");
        REQUIRE(result.has_value() == false);
    }

    SECTION("standalone boolean is rejected") {
        auto result = fast_parse("true");
        REQUIRE(result.has_value() == false);
    }

    SECTION("standalone null is rejected") {
        auto result = fast_parse("null");
        REQUIRE(result.has_value() == false);
    }

    SECTION("wrapped values work fine") {
        auto result = fast_parse(R"({"value": "just a string"})");
        REQUIRE(result.has_value());
        REQUIRE((*result)["value"] == "just a string");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON-RPC Message Tests (Real-world use case)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("fast_parse handles JSON-RPC request", "[json][simdjson][jsonrpc]") {
    auto result = fast_parse(R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/list",
        "params": {}
    })");

    REQUIRE(result.has_value());

    const auto& j = *result;
    REQUIRE(j["jsonrpc"] == "2.0");
    REQUIRE(j["id"] == 1);
    REQUIRE(j["method"] == "tools/list");
    REQUIRE(j["params"].is_object());
}

TEST_CASE("fast_parse handles JSON-RPC response", "[json][simdjson][jsonrpc]") {
    auto result = fast_parse(R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "tools": [
                {"name": "read_file", "description": "Read a file"},
                {"name": "write_file", "description": "Write a file"}
            ]
        }
    })");

    REQUIRE(result.has_value());

    const auto& j = *result;
    REQUIRE(j["result"]["tools"].size() == 2);
    REQUIRE(j["result"]["tools"][0]["name"] == "read_file");
}

TEST_CASE("fast_parse handles JSON-RPC error", "[json][simdjson][jsonrpc]") {
    auto result = fast_parse(R"({
        "jsonrpc": "2.0",
        "id": 1,
        "error": {
            "code": -32600,
            "message": "Invalid Request"
        }
    })");

    REQUIRE(result.has_value());

    const auto& j = *result;
    REQUIRE(j["error"]["code"] == -32600);
    REQUIRE(j["error"]["message"] == "Invalid Request");
}

// ─────────────────────────────────────────────────────────────────────────────
// FastJsonParser Instance Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("FastJsonParser can be reused", "[json][simdjson]") {
    FastJsonParser parser;

    auto result1 = parser.parse(R"({"first": 1})");
    REQUIRE(result1.has_value());
    REQUIRE((*result1)["first"] == 1);

    auto result2 = parser.parse(R"({"second": 2})");
    REQUIRE(result2.has_value());
    REQUIRE((*result2)["second"] == 2);

    auto result3 = parser.parse(R"({"third": 3})");
    REQUIRE(result3.has_value());
    REQUIRE((*result3)["third"] == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Implementation Info
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("fast_json_implementation returns valid name", "[json][simdjson]") {
    std::string impl = fast_json_implementation();

    // Should be one of: haswell, westmere, arm64, fallback, etc.
    REQUIRE(impl.empty() == false);

    // Log it for informational purposes
    INFO("simdjson implementation: " << impl);
}

