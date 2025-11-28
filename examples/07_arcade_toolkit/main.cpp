// Example 07: Arcade AI Toolkit Integration
// 
// Connect to Arcade AI MCP servers for enterprise tool orchestration.

#include <mcpp/async/async_mcp_client.hpp>
#include <mcpp/async/async_process_transport.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <cstdlib>

using namespace mcpp;
using namespace mcpp::async;
using Json = nlohmann::json;

// Helper to print tool results
void print_result(const CallToolResult& result) {
    for (const auto& content : result.content) {
        if (content.type == "text" && content.text) {
            // Try to parse as JSON for pretty printing
            try {
                auto json = Json::parse(*content.text);
                std::cout << json.dump(2) << "\n";
            } catch (...) {
                std::cout << *content.text << "\n";
            }
        }
    }
}

asio::awaitable<int> run_github_toolkit(asio::io_context& io) {
    std::cout << "=== Arcade AI GitHub Toolkit Example ===\n\n";
    
    // Check for token
    const char* token = std::getenv("GITHUB_PERSONAL_ACCESS_TOKEN");
    if (!token) {
        std::cerr << "ERROR: GITHUB_PERSONAL_ACCESS_TOKEN not set\n";
        std::cerr << "Please set it: export GITHUB_PERSONAL_ACCESS_TOKEN=\"ghp_xxx\"\n";
        co_return 1;
    }
    
    // Check for toolkit path
    const char* toolkit_path = std::getenv("ARCADE_GITHUB_TOOLKIT_PATH");
    std::string working_dir = toolkit_path ? toolkit_path : ".";
    
    std::cout << "Token: ****" << std::string(token).substr(std::string(token).length() - 4) << "\n";
    std::cout << "Toolkit path: " << working_dir << "\n\n";
    
    // 1. Configure transport for Arcade GitHub toolkit
    AsyncProcessTransportConfig config;
    config.command = "python";
    config.args = {"-m", "arcade_mcp_server", "stdio", "--tool-package", "github"};
    config.working_directory = working_dir;
    config.environment = {{"GITHUB_PERSONAL_ACCESS_TOKEN", token}};
    config.use_content_length_framing = false;
    
    std::cout << "Starting Arcade GitHub toolkit...\n";
    
    // 2. Create transport
    auto transport = std::make_unique<AsyncProcessTransport>(io, config);
    
    // 3. Configure client
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_info = {"arcade-example", "1.0.0"};
    client_config.enable_circuit_breaker = true;
    
    AsyncMcpClient client(std::move(transport), client_config);
    
    // 4. Register progress handler
    client.on_progress([](const ProgressNotification& p) {
        std::cout << "[Progress] " << p.progress;
        if (p.total) std::cout << "/" << *p.total;
        std::cout << "\n";
    });
    
    // 5. Connect
    auto connect_result = co_await client.connect();
    if (!connect_result) {
        std::cerr << "Failed to connect: " << connect_result.error().message << "\n";
        co_return 1;
    }
    
    std::cout << "Connected to: " << connect_result->server_info.name << "\n\n";
    
    // 6. List available tools
    std::cout << "=== Available GitHub Tools ===\n";
    auto tools = co_await client.list_tools();
    if (tools) {
        for (const auto& tool : tools->tools) {
            std::cout << "  - " << tool.name;
            if (tool.description) {
                // Truncate long descriptions
                std::string desc = *tool.description;
                if (desc.length() > 60) {
                    desc = desc.substr(0, 57) + "...";
                }
                std::cout << ": " << desc;
            }
            std::cout << "\n";
        }
    } else {
        std::cerr << "Failed to list tools: " << tools.error().message << "\n";
    }
    std::cout << "\n";
    
    // 7. Try to call Index tool (if available)
    std::cout << "=== Calling Index Tool ===\n";
    auto index = co_await client.call_tool("Github_Index", {});
    if (index) {
        std::cout << "Index tool available - toolkit info retrieved\n";
        // Don't print full index, it's usually very long
    } else {
        std::cout << "Index tool not available (this is OK)\n";
    }
    std::cout << "\n";
    
    // 8. Get authenticated user info
    std::cout << "=== Get Authenticated User ===\n";
    auto me = co_await client.call_tool("get_me", {});
    if (!me) {
        // Try alternative name
        me = co_await client.call_tool("github_get_me", {});
    }
    if (!me) {
        // Try another alternative
        me = co_await client.call_tool("Github_GetMe", {});
    }
    
    if (me) {
        print_result(*me);
    } else {
        std::cerr << "Failed to get user: " << me.error().message << "\n";
        std::cout << "(Tool name may vary - check available tools above)\n";
    }
    std::cout << "\n";
    
    // 9. Search repositories
    std::cout << "=== Search Repositories ===\n";
    std::cout << "Searching for: language:cpp stars:>5000\n\n";
    
    auto search = co_await client.call_tool("search_repositories", {
        {"query", "language:cpp stars:>5000"},
        {"per_page", 5}
    });
    if (!search) {
        search = co_await client.call_tool("github_search_repositories", {
            {"query", "language:cpp stars:>5000"}
        });
    }
    
    if (search) {
        print_result(*search);
    } else {
        std::cerr << "Failed to search: " << search.error().message << "\n";
    }
    std::cout << "\n";
    
    // 10. Circuit breaker stats
    std::cout << "=== Circuit Breaker Stats ===\n";
    auto stats = client.circuit_stats();
    std::cout << "Total requests: " << stats.total_requests << "\n";
    std::cout << "Successful: " << stats.successful_requests << "\n";
    std::cout << "Failed: " << stats.failed_requests << "\n";
    std::cout << "Circuit state: " << (client.is_circuit_open() ? "OPEN" : "CLOSED") << "\n\n";
    
    // 11. Disconnect
    std::cout << "Disconnecting...\n";
    co_await client.disconnect();
    std::cout << "Done!\n";
    
    co_return 0;
}

int main() {
    try {
        asio::io_context io;
        
        int exit_code = 0;
        
        asio::co_spawn(io,
            [&io, &exit_code]() -> asio::awaitable<void> {
                exit_code = co_await run_github_toolkit(io);
            },
            asio::detached
        );
        
        io.run();
        
        return exit_code;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}

