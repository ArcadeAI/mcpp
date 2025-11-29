#include "mcpp/client/arcade_client.hpp"
#include "mcpp/log/logger.hpp"

#include <cstdlib>
#include <stdexcept>

namespace mcpp::arcade {

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::string get_env(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Configuration Conversion
// ─────────────────────────────────────────────────────────────────────────────

McpClientConfig to_mcp_config(const ArcadeConfig& config) {
    McpClientConfig mcp_config;
    
    // Client info
    mcp_config.client_name = config.client_name;
    mcp_config.client_version = config.client_version;
    
    // Transport configuration
    mcp_config.transport.base_url = config.build_url();
    mcp_config.transport.connect_timeout = config.connect_timeout;
    mcp_config.transport.read_timeout = config.read_timeout;
    mcp_config.transport.max_retries = config.max_retries;
    
    // Arcade authentication headers
    mcp_config.transport.with_bearer_token(config.api_key);
    mcp_config.transport.with_header("Arcade-User-ID", config.user_id);
    
    // Disable SSE stream for simple request-response pattern
    // (Arcade gateways use standard HTTP, not long-lived SSE connections)
    mcp_config.transport.auto_open_sse_stream = false;
    
    // Request timeout
    mcp_config.request_timeout = config.request_timeout;
    
    // Auto-initialize
    mcp_config.auto_initialize = config.auto_initialize;
    
    // Circuit breaker
    mcp_config.enable_circuit_breaker = config.enable_circuit_breaker;
    
    return mcp_config;
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory Functions
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<McpClient> create_client(const ArcadeConfig& config) {
    // Validate configuration
    if (!config.is_valid()) {
        throw std::invalid_argument("Invalid ArcadeConfig: " + config.validation_error());
    }
    
    MCPP_LOG_DEBUG("Creating Arcade client for gateway: " + config.gateway_slug);
    
    return std::make_unique<McpClient>(to_mcp_config(config));
}

McpResult<std::pair<std::unique_ptr<McpClient>, InitializeResult>> 
connect(const ArcadeConfig& config) {
    // Validate configuration
    if (!config.is_valid()) {
        return tl::unexpected(McpClientError::transport_error(
            "Invalid ArcadeConfig: " + config.validation_error()
        ));
    }
    
    get_logger().info_fmt("Connecting to Arcade gateway: {}", config.gateway_slug);
    
    // Create client
    auto client = std::make_unique<McpClient>(to_mcp_config(config));
    
    // Connect and initialize
    auto connect_result = client->connect();
    if (!connect_result) {
        return tl::unexpected(connect_result.error());
    }
    
    get_logger().info_fmt("Connected to Arcade gateway: {} (server: {})", 
                          config.gateway_slug, 
                          connect_result->server_info.name);
    
    return std::make_pair(std::move(client), std::move(*connect_result));
}

ArcadeConfig config_from_env(const std::string& gateway_slug) {
    ArcadeConfig config;
    
    // Required: API key
    config.api_key = get_env("ARCADE_API_KEY");
    
    // Required: User ID
    config.user_id = get_env("ARCADE_USER_ID");
    
    // Gateway: from parameter or environment
    if (!gateway_slug.empty()) {
        config.gateway_slug = gateway_slug;
    } else {
        config.gateway_slug = get_env("ARCADE_GATEWAY");
    }
    
    // Optional: Custom base URL
    std::string custom_base_url = get_env("ARCADE_BASE_URL");
    if (!custom_base_url.empty()) {
        config.base_url = custom_base_url;
    }
    
    return config;
}

}  // namespace mcpp::arcade

