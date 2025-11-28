#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Fast JSON Parser using simdjson
// ─────────────────────────────────────────────────────────────────────────────
//
// This provides a high-performance JSON parsing layer using simdjson (SIMD-
// accelerated), while still producing nlohmann::json objects for compatibility
// with the rest of the codebase.
//
// WHY TWO LIBRARIES?
// - simdjson: Fastest JSON parsing (2-10x faster than alternatives)
// - nlohmann/json: Best ergonomics for manipulation and serialization
//
// We use simdjson for parsing incoming data (hot path) and nlohmann for
// building outgoing messages (less performance-critical).
//
// USAGE:
//   // Fast parsing (uses simdjson internally)
//   auto result = mcpp::fast_parse(json_string);
//   if (result.has_value()) {
//       nlohmann::json& doc = *result;
//       // Use as normal nlohmann::json
//   }
//
//   // For serialization, use nlohmann directly:
//   nlohmann::json obj = {{"key", "value"}};
//   std::string output = obj.dump();
//
// ─────────────────────────────────────────────────────────────────────────────

#include <nlohmann/json.hpp>
#include <simdjson.h>
#include <tl/expected.hpp>

#include <string>
#include <string_view>

namespace mcpp {

// Error type for JSON parsing failures
struct JsonParseError {
    std::string message;
    std::size_t position{0};  // Byte position where error occurred

    JsonParseError() = default;
    explicit JsonParseError(std::string msg, std::size_t pos = 0)
        : message(std::move(msg))
        , position(pos)
    {}
};

// Result type for JSON parsing
using JsonResult = tl::expected<nlohmann::json, JsonParseError>;

// ─────────────────────────────────────────────────────────────────────────────
// Fast JSON Parser Class
// ─────────────────────────────────────────────────────────────────────────────

class FastJsonParser {
public:
    FastJsonParser() = default;

    // Parse JSON string to nlohmann::json
    // Thread-safe: each instance has its own parser state
    [[nodiscard]] JsonResult parse(std::string_view json_str);

    // Parse with padding (more efficient for large documents)
    // The input must have SIMDJSON_PADDING bytes available after the content
    [[nodiscard]] JsonResult parse_padded(simdjson::padded_string_view json_str);

private:
    simdjson::ondemand::parser parser_;

    // Convert simdjson value to nlohmann::json recursively
    [[nodiscard]] nlohmann::json convert(simdjson::ondemand::value value);
    [[nodiscard]] nlohmann::json convert_object(simdjson::ondemand::object obj);
    [[nodiscard]] nlohmann::json convert_array(simdjson::ondemand::array arr);
};

// ─────────────────────────────────────────────────────────────────────────────
// Convenience Functions
// ─────────────────────────────────────────────────────────────────────────────

// Thread-local fast parser for convenience
// Use this for simple cases; create your own FastJsonParser for high-throughput
[[nodiscard]] JsonResult fast_parse(std::string_view json_str);

// Check if simdjson is using SIMD acceleration on this platform
// Returns a string like "haswell", "westmere", "arm64", "fallback", etc.
[[nodiscard]] std::string fast_json_implementation();

}  // namespace mcpp

