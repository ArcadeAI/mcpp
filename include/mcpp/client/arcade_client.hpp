#pragma once

#include "mcpp/client/mcp_client.hpp"
#include "mcpp/transport/http_transport_config.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace mcpp {

// ═══════════════════════════════════════════════════════════════════════════
// Arcade AI Gateway Configuration
// ═══════════════════════════════════════════════════════════════════════════
// Simplified configuration for connecting to Arcade AI MCP gateways.
//
// Arcade handles authentication and secrets server-side, so you only need:
// - Your Arcade API key
// - Your user ID  
// - The gateway slug
//
// No local secrets or tool packages needed - Arcade manages everything!

struct ArcadeConfig {
    // ─────────────────────────────────────────────────────────────────────────
    // Required Settings
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Gateway slug (e.g., "ultracoolserver", "my-github-tools")
    /// The full URL will be: https://api.arcade.dev/mcp/<gateway_slug>
    std::string gateway_slug;
    
    /// Arcade API key (starts with "arc_")
    /// Get one from https://arcade.dev
    std::string api_key;
    
    /// User ID for this session (typically email)
    /// Used by Arcade for authentication and rate limiting
    std::string user_id;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Optional Settings
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Custom Arcade API base URL (default: https://api.arcade.dev)
    /// Useful for on-premise deployments or testing
    std::string base_url = "https://api.arcade.dev";
    
    /// Client name to report to server (default: "mcpp-arcade")
    std::string client_name = "mcpp-arcade";
    
    /// Client version to report to server
    std::string client_version = "1.0.0";
    
    /// Connection timeout
    std::chrono::milliseconds connect_timeout{10000};  // 10 seconds
    
    /// Read timeout (time to wait for response)
    std::chrono::milliseconds read_timeout{30000};  // 30 seconds
    
    /// Request timeout (0 = no limit)
    std::chrono::milliseconds request_timeout{60000};  // 60 seconds
    
    /// Maximum retry attempts for failed requests
    std::size_t max_retries{3};
    
    /// Enable circuit breaker for resilience
    bool enable_circuit_breaker{true};
    
    /// Auto-initialize on connect (send initialize handshake)
    bool auto_initialize{true};
    
    // ─────────────────────────────────────────────────────────────────────────
    // Builder Methods (for fluent configuration)
    // ─────────────────────────────────────────────────────────────────────────
    
    ArcadeConfig& with_gateway(const std::string& slug) {
        gateway_slug = slug;
        return *this;
    }
    
    ArcadeConfig& with_api_key(const std::string& key) {
        api_key = key;
        return *this;
    }
    
    ArcadeConfig& with_user_id(const std::string& id) {
        user_id = id;
        return *this;
    }
    
    ArcadeConfig& with_base_url(const std::string& url) {
        base_url = url;
        return *this;
    }
    
    ArcadeConfig& with_client_info(const std::string& name, const std::string& version) {
        client_name = name;
        client_version = version;
        return *this;
    }
    
    ArcadeConfig& with_connect_timeout(std::chrono::milliseconds timeout) {
        connect_timeout = timeout;
        return *this;
    }
    
    ArcadeConfig& with_read_timeout(std::chrono::milliseconds timeout) {
        read_timeout = timeout;
        return *this;
    }
    
    ArcadeConfig& with_request_timeout(std::chrono::milliseconds timeout) {
        request_timeout = timeout;
        return *this;
    }
    
    ArcadeConfig& with_max_retries(std::size_t retries) {
        max_retries = retries;
        return *this;
    }
    
    ArcadeConfig& with_circuit_breaker(bool enabled) {
        enable_circuit_breaker = enabled;
        return *this;
    }
    
    ArcadeConfig& with_auto_initialize(bool enabled) {
        auto_initialize = enabled;
        return *this;
    }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Validation
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Check if configuration is valid
    [[nodiscard]] bool is_valid() const {
        return !gateway_slug.empty() && !api_key.empty() && !user_id.empty();
    }
    
    /// Get validation error message (empty if valid)
    [[nodiscard]] std::string validation_error() const {
        if (gateway_slug.empty()) return "Gateway slug is required";
        if (api_key.empty()) return "API key is required";
        if (user_id.empty()) return "User ID is required";
        return "";
    }
    
    /// Build the full gateway URL
    [[nodiscard]] std::string build_url() const {
        std::string url = base_url;
        // Remove trailing slash if present
        if (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        return url + "/mcp/" + gateway_slug;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Arcade Client Factory
// ═══════════════════════════════════════════════════════════════════════════
// Factory functions for creating MCP clients connected to Arcade gateways.

namespace arcade {

/// Create an MCP client configured for an Arcade gateway
/// 
/// Example:
///   auto client = arcade::connect({
///       .gateway_slug = "ultracoolserver",
///       .api_key = "arc_xxx",
///       .user_id = "user@example.com"
///   });
///   
///   auto tools = client->list_tools();
[[nodiscard]] std::unique_ptr<McpClient> create_client(const ArcadeConfig& config);

/// Connect to an Arcade gateway and return initialized client
/// Throws std::invalid_argument if config is invalid
/// Returns error result if connection fails
///
/// Example:
///   auto result = arcade::connect({
///       .gateway_slug = "ultracoolserver",
///       .api_key = "arc_xxx", 
///       .user_id = "user@example.com"
///   });
///   
///   if (result) {
///       auto& [client, init_result] = *result;
///       std::cout << "Connected to: " << init_result.server_info.name << "\n";
///   }
[[nodiscard]] McpResult<std::pair<std::unique_ptr<McpClient>, InitializeResult>> 
connect(const ArcadeConfig& config);

/// Create ArcadeConfig from environment variables
/// Reads: ARCADE_API_KEY, ARCADE_USER_ID, ARCADE_GATEWAY (optional)
///
/// Example:
///   export ARCADE_API_KEY=arc_xxx
///   export ARCADE_USER_ID=user@example.com
///   
///   auto config = arcade::config_from_env("my-gateway");
///   auto client = arcade::connect(config);
[[nodiscard]] ArcadeConfig config_from_env(const std::string& gateway_slug = "");

/// Convert ArcadeConfig to McpClientConfig
[[nodiscard]] McpClientConfig to_mcp_config(const ArcadeConfig& config);

}  // namespace arcade

}  // namespace mcpp

