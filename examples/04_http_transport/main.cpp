// Example 04: HTTP Transport
// 
// Connect to remote MCP servers over HTTP with SSE.

#include <mcpp/transport/http_transport.hpp>
#include <mcpp/client/mcp_client.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <cstdlib>

using namespace mcpp;
using Json = nlohmann::json;

int main() {
    std::cout << "=== HTTP Transport Example ===\n\n";
    
    // Get configuration from environment
    const char* url_env = std::getenv("MCP_SERVER_URL");
    const char* token_env = std::getenv("MCP_TOKEN");
    
    if (!url_env) {
        std::cerr << "Please set MCP_SERVER_URL environment variable\n";
        std::cerr << "Example: export MCP_SERVER_URL=\"https://api.example.com/mcp/\"\n";
        return 1;
    }
    
    std::string base_url = url_env;
    std::string token = token_env ? token_env : "";
    
    std::cout << "Server URL: " << base_url << "\n";
    std::cout << "Token: " << (token.empty() ? "(none)" : "****") << "\n\n";
    
    // 1. Configure HTTP transport
    HttpClientConfig config;
    config.base_url = base_url;
    config.timeout = std::chrono::seconds(30);
    config.connect_timeout = std::chrono::seconds(10);
    
    if (!token.empty()) {
        config.headers = {{"Authorization", "Bearer " + token}};
    }
    
    // 2. Create transport
    auto transport = std::make_unique<HttpTransport>(config);
    
    // 3. Configure client with circuit breaker
    McpClientConfig client_config;
    client_config.enable_circuit_breaker = true;
    client_config.circuit_breaker.failure_threshold = 3;
    client_config.circuit_breaker.recovery_timeout = std::chrono::seconds(10);
    
    McpClient client(std::move(transport), client_config);
    
    // 4. Connect
    std::cout << "Connecting...\n";
    auto connect_result = client.connect();
    if (!connect_result) {
        std::cerr << "Failed to connect: " << connect_result.error().message << "\n";
        return 1;
    }
    std::cout << "Connected!\n\n";
    
    // 5. Initialize
    std::cout << "Initializing...\n";
    auto init = client.initialize();
    if (!init) {
        std::cerr << "Failed to initialize: " << init.error().message << "\n";
        return 1;
    }
    
    std::cout << "Server: " << init->server_info.name;
    if (init->server_info.version) {
        std::cout << " v" << *init->server_info.version;
    }
    std::cout << "\n";
    std::cout << "Protocol: " << init->protocol_version << "\n\n";
    
    // 6. List tools
    std::cout << "=== Available Tools ===\n";
    auto tools = client.list_tools();
    if (tools) {
        if (tools->tools.empty()) {
            std::cout << "  (no tools)\n";
        } else {
            for (const auto& tool : tools->tools) {
                std::cout << "  - " << tool.name;
                if (tool.description) {
                    std::cout << ": " << *tool.description;
                }
                std::cout << "\n";
            }
        }
    } else {
        std::cerr << "  Failed: " << tools.error().message << "\n";
    }
    std::cout << "\n";
    
    // 7. List prompts
    std::cout << "=== Available Prompts ===\n";
    auto prompts = client.list_prompts();
    if (prompts) {
        if (prompts->prompts.empty()) {
            std::cout << "  (no prompts)\n";
        } else {
            for (const auto& prompt : prompts->prompts) {
                std::cout << "  - " << prompt.name;
                if (prompt.description) {
                    std::cout << ": " << *prompt.description;
                }
                std::cout << "\n";
            }
        }
    } else {
        std::cerr << "  Failed: " << prompts.error().message << "\n";
    }
    std::cout << "\n";
    
    // 8. Ping
    std::cout << "=== Ping ===\n";
    auto ping = client.ping();
    if (ping) {
        std::cout << "  Pong!\n";
    } else {
        std::cerr << "  Failed: " << ping.error().message << "\n";
    }
    std::cout << "\n";
    
    // 9. Show circuit breaker stats
    std::cout << "=== Circuit Breaker Stats ===\n";
    auto stats = client.circuit_stats();
    std::cout << "  State: " << (client.is_circuit_open() ? "OPEN" : "CLOSED") << "\n";
    std::cout << "  Total requests: " << stats.total_requests << "\n";
    std::cout << "  Successful: " << stats.successful_requests << "\n";
    std::cout << "  Failed: " << stats.failed_requests << "\n";
    std::cout << "  Rejected: " << stats.rejected_requests << "\n";
    std::cout << "\n";
    
    // 10. Disconnect
    std::cout << "Disconnecting...\n";
    client.disconnect();
    std::cout << "Done!\n";
    
    return 0;
}

