// Example 07: Arcade AI MCP Gateway Integration
// 
// Connect to Arcade AI MCP gateways over HTTP for enterprise tool orchestration.
// 
// Arcade gateways handle authentication and secrets server-side, so you only need:
// - Your Arcade API key
// - Your user ID
// - The gateway slug
//
// No local secrets or tool packages needed - Arcade manages everything!
//
// Usage:
//   export ARCADE_API_KEY="arc_xxx"
//   export ARCADE_USER_ID="user@example.com"
//   ./arcade_example
//
// Or use the CLI:
//   mcpp-cli --arcade my-gateway --arcade-key arc_xxx --arcade-user user@example.com --list-tools

#include <mcpp/transport/http_transport.hpp>
#include <mcpp/protocol/mcp_types.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <cstdlib>
#include <chrono>
#include <thread>

using namespace mcpp;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Simple MCP Client for HTTP Transport
// ═══════════════════════════════════════════════════════════════════════════

class SimpleMcpClient {
public:
    explicit SimpleMcpClient(HttpTransport& transport)
        : transport_(transport)
    {}

    // Send request and wait for response
    HttpResult<Json> request(const std::string& method, const Json& params = Json::object()) {
        Json req = {
            {"jsonrpc", "2.0"},
            {"id", ++request_id_},
            {"method", method}
        };
        if (!params.empty()) {
            req["params"] = params;
        }

        auto send_result = transport_.send(req);
        if (!send_result) {
            return tl::unexpected(send_result.error());
        }

        // Read response (skip notifications)
        while (true) {
            auto result = transport_.receive();
            if (!result) {
                return result;
            }
            if (result->contains("id")) {
                return result;
            }
            // Skip notifications
        }
    }

    // Initialize the MCP connection
    HttpResult<InitializeResult> initialize(const std::string& client_name = "arcade-example") {
        InitializeParams params;
        params.client_info = {client_name, "1.0.0"};

        auto result = request("initialize", params.to_json());
        if (!result) {
            return tl::unexpected(result.error());
        }

        if (result->contains("error")) {
            return tl::unexpected(HttpTransportError{
                HttpTransportError::Code::InvalidResponse,
                (*result)["error"]["message"].get<std::string>(),
                std::nullopt
            });
        }

        // Send initialized notification
        Json notification = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/initialized"}
        };
        transport_.send(notification);

        return InitializeResult::from_json((*result)["result"]);
    }

    // List available tools
    HttpResult<ListToolsResult> list_tools() {
        auto result = request("tools/list");
        if (!result) {
            return tl::unexpected(result.error());
        }
        if (result->contains("error")) {
            return tl::unexpected(HttpTransportError{
                HttpTransportError::Code::InvalidResponse,
                (*result)["error"]["message"].get<std::string>(),
                std::nullopt
            });
        }
        return ListToolsResult::from_json((*result)["result"]);
    }

    // Call a tool
    HttpResult<CallToolResult> call_tool(const std::string& name, const Json& args = Json::object()) {
        Json params = {
            {"name", name},
            {"arguments", args}
        };
        auto result = request("tools/call", params);
        if (!result) {
            return tl::unexpected(result.error());
        }
        if (result->contains("error")) {
            return tl::unexpected(HttpTransportError{
                HttpTransportError::Code::InvalidResponse,
                (*result)["error"]["message"].get<std::string>(),
                std::nullopt
            });
        }
        return CallToolResult::from_json((*result)["result"]);
    }

private:
    HttpTransport& transport_;
    int request_id_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

std::string get_env(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

void print_tool_result(const CallToolResult& result) {
    for (const auto& content : result.content) {
        if (std::holds_alternative<TextContent>(content)) {
            const auto& text = std::get<TextContent>(content).text;
            // Try to pretty-print JSON
            try {
                auto json = Json::parse(text);
                std::cout << json.dump(2) << "\n";
            } catch (...) {
                std::cout << text << "\n";
            }
        } else if (std::holds_alternative<ImageContent>(content)) {
            std::cout << "[Image: " << std::get<ImageContent>(content).mime_type << "]\n";
        } else if (std::holds_alternative<EmbeddedResource>(content)) {
            std::cout << "[Resource: " << std::get<EmbeddedResource>(content).uri << "]\n";
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    std::cout << "=== Arcade AI MCP Gateway Example ===\n\n";
    
    // Get configuration from environment or command line
    std::string gateway_slug = argc > 1 ? argv[1] : get_env("ARCADE_GATEWAY", "ultracoolserver");
    std::string api_key = get_env("ARCADE_API_KEY");
    std::string user_id = get_env("ARCADE_USER_ID");
    
    // Validate required configuration
    if (api_key.empty()) {
        std::cerr << "ERROR: ARCADE_API_KEY not set\n";
        std::cerr << "Please set it: export ARCADE_API_KEY=\"arc_xxx\"\n";
        return 1;
    }
    
    if (user_id.empty()) {
        std::cerr << "ERROR: ARCADE_USER_ID not set\n";
        std::cerr << "Please set it: export ARCADE_USER_ID=\"user@example.com\"\n";
        return 1;
    }
    
    std::cout << "Gateway: " << gateway_slug << "\n";
    std::cout << "User: " << user_id << "\n";
    std::cout << "API Key: ****" << api_key.substr(api_key.length() - 4) << "\n\n";
    
    try {
        // 1. Configure HTTP transport for Arcade gateway
        //
        // Key difference from stdio: No local secrets needed!
        // Arcade handles authentication and tool credentials server-side.
        HttpTransportConfig config;
        config.base_url = "https://api.arcade.dev/mcp/" + gateway_slug;
        
        // Arcade authentication headers
        config.with_bearer_token(api_key);
        config.with_header("Arcade-User-ID", user_id);
        
        // Disable SSE stream for simple request-response pattern
        config.auto_open_sse_stream = false;
        
        // Reasonable timeouts for API calls
        config.connect_timeout = std::chrono::milliseconds{10000};
        config.read_timeout = std::chrono::milliseconds{30000};
        
        std::cout << "Connecting to Arcade gateway...\n";
        
        // 2. Create and start transport
        HttpTransport transport(config);
        transport.start();
        
        // 3. Create MCP client
        SimpleMcpClient client(transport);
        
        // 4. Initialize connection
        auto init_result = client.initialize();
        if (!init_result) {
            std::cerr << "Failed to initialize: " << init_result.error().message << "\n";
            return 1;
        }
        
        std::cout << "Connected to: " << init_result->server_info.name 
                  << " v" << init_result->server_info.version << "\n\n";
        
        // 5. List available tools
        std::cout << "=== Available Tools ===\n";
        auto tools = client.list_tools();
        if (tools) {
            std::cout << "Found " << tools->tools.size() << " tools:\n\n";
            for (const auto& tool : tools->tools) {
                std::cout << "  • " << tool.name;
                if (tool.description) {
                    std::string desc = *tool.description;
                    if (desc.length() > 60) {
                        desc = desc.substr(0, 57) + "...";
                    }
                    std::cout << "\n    " << desc;
                }
                std::cout << "\n";
            }
        } else {
            std::cerr << "Failed to list tools: " << tools.error().message << "\n";
        }
        std::cout << "\n";
        
        // 6. Call Index tool (if available)
        std::cout << "=== Calling Index Tool ===\n";
        auto index = client.call_tool("Github_Index", {});
        if (index) {
            std::cout << "Index tool available - toolkit info retrieved\n";
        } else {
            std::cout << "Index tool not available (expected for some gateways)\n";
        }
        std::cout << "\n";
        
        // 7. Try to get authenticated user info
        std::cout << "=== Get Authenticated User ===\n";
        
        // Tool names follow the pattern: ToolkitName_ToolName
        // Try common patterns
        auto me = client.call_tool("Github_GetMe", {});
        if (!me) {
            me = client.call_tool("github_get_me", {});
        }
        if (!me) {
            me = client.call_tool("GetMe", {});
        }
        
        if (me) {
            print_tool_result(*me);
        } else {
            std::cout << "GetMe tool not available: " << me.error().message << "\n";
            std::cout << "(Check available tools above for correct name)\n";
        }
        std::cout << "\n";
        
        // 8. Search repositories (if GitHub tools available)
        std::cout << "=== Search Repositories ===\n";
        std::cout << "Searching for: language:cpp stars:>5000\n\n";
        
        auto search = client.call_tool("Github_SearchRepositories", {
            {"query", "language:cpp stars:>5000"},
            {"per_page", 5}
        });
        if (!search) {
            search = client.call_tool("search_repositories", {
                {"query", "language:cpp stars:>5000"}
            });
        }
        
        if (search) {
            print_tool_result(*search);
        } else {
            std::cout << "Search not available: " << search.error().message << "\n";
        }
        std::cout << "\n";
        
        // 9. Cleanup
        std::cout << "Disconnecting...\n";
        transport.stop();
        std::cout << "Done!\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
