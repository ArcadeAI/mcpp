#ifndef MCPP_CLIENT_HANDLER_UTILS_HPP
#define MCPP_CLIENT_HANDLER_UTILS_HPP

// ═══════════════════════════════════════════════════════════════════════════
// Handler Utilities
// ═══════════════════════════════════════════════════════════════════════════
// Shared utilities for request handlers to avoid duplication between
// synchronous and asynchronous MCP clients.

#include "mcpp/protocol/mcp_types.hpp"
#include "mcpp/security/url_validator.hpp"
#include "mcpp/log/logger.hpp"

#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

#include <optional>
#include <string>

namespace mcpp {

using Json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Elicitation URL Validation
// ─────────────────────────────────────────────────────────────────────────────

/// Result of elicitation URL validation
struct ElicitationUrlValidation {
    bool should_decline{false};   ///< If true, decline the elicitation
    std::string decline_reason;   ///< Reason for declining (for logging)
    std::optional<std::string> warning;  ///< Warning to log (if proceeding)
};

/// Validate an elicitation URL for security
/// Returns validation result with decline flag and optional warning
[[nodiscard]] inline ElicitationUrlValidation validate_elicitation_url(
    const std::string& url
) {
    auto validation = security::validate_url(url);
    
    if (!validation.is_safe) {
        return {
            true,  // should_decline
            validation.error.value_or("blocked by security policy"),
            std::nullopt
        };
    }
    
    return {
        false,  // don't decline
        "",
        validation.warning
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON-RPC Response Building
// ─────────────────────────────────────────────────────────────────────────────

/// Build a JSON-RPC success response
[[nodiscard]] inline Json build_success_response(
    const Json& request_id,
    const Json& result
) {
    return {
        {"jsonrpc", "2.0"},
        {"id", request_id},
        {"result", result}
    };
}

/// Build a JSON-RPC error response
[[nodiscard]] inline Json build_error_response(
    const Json& request_id,
    int error_code,
    const std::string& message
) {
    return {
        {"jsonrpc", "2.0"},
        {"id", request_id},
        {"error", {
            {"code", error_code},
            {"message", message}
        }}
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampling Request Parsing
// ─────────────────────────────────────────────────────────────────────────────

/// Parse sampling request parameters
[[nodiscard]] inline CreateMessageParams parse_sampling_request(const Json& params) {
    return CreateMessageParams::from_json(params);
}

// ─────────────────────────────────────────────────────────────────────────────
// Elicitation Request Parsing
// ─────────────────────────────────────────────────────────────────────────────

/// Determine elicitation mode from params
[[nodiscard]] inline std::string get_elicitation_mode(const Json& params) {
    return params.value("mode", "form");
}

/// Check if elicitation mode is URL
[[nodiscard]] inline bool is_url_elicitation(const std::string& mode) {
    return mode == "url";
}

}  // namespace mcpp

#endif  // MCPP_CLIENT_HANDLER_UTILS_HPP

