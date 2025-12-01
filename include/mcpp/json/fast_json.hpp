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
// Fast JSON Parser Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct FastJsonConfig {
    // Maximum nesting depth to prevent stack overflow
    // Default 64 matches most JSON parsers (Python's json module uses 1000)
    std::size_t max_depth{64};
};

// ─────────────────────────────────────────────────────────────────────────────
// Fast JSON Parser Class
// ─────────────────────────────────────────────────────────────────────────────

class FastJsonParser {
public:
    FastJsonParser() = default;
    explicit FastJsonParser(FastJsonConfig config) : config_(config) {}

    // Parse JSON string to nlohmann::json
    // Thread-safe: each instance has its own parser state
    [[nodiscard]] JsonResult parse(std::string_view json_str);

    // Parse with padding (more efficient for large documents)
    // The input must have SIMDJSON_PADDING bytes available after the content
    [[nodiscard]] JsonResult parse_padded(simdjson::padded_string_view json_str);
    
    // Get/set configuration
    [[nodiscard]] const FastJsonConfig& config() const noexcept { return config_; }
    void set_config(FastJsonConfig config) noexcept { config_ = config; }

private:
    simdjson::ondemand::parser parser_;
    FastJsonConfig config_;

    // Convert simdjson value to nlohmann::json with depth tracking
    [[nodiscard]] JsonResult convert(simdjson::ondemand::value value, std::size_t depth);
    [[nodiscard]] JsonResult convert_object(simdjson::ondemand::object obj, std::size_t depth);
    [[nodiscard]] JsonResult convert_array(simdjson::ondemand::array arr, std::size_t depth);
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

