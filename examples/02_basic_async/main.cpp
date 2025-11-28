// Example 02: Basic Asynchronous MCP Client
// 
// Demonstrates C++20 coroutines with AsyncMcpClient for non-blocking operations.

#include <mcpp/async/async_mcp_client.hpp>
#include <mcpp/async/async_process_transport.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace mcpp;
using namespace mcpp::async;
using Json = nlohmann::json;

// Main client coroutine
asio::awaitable<int> run_client(asio::io_context& io) {
    std::cout << "=== Basic Asynchronous MCP Client Example ===\n\n";
    
    // 1. Configure transport
    AsyncProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
    config.use_content_length_framing = false;
    
    std::cout << "Starting server: " << config.command;
    for (const auto& a : config.args) std::cout << " " << a;
    std::cout << "\n\n";
    
    // 2. Create transport
    auto transport = std::make_unique<AsyncProcessTransport>(io, config);
    
    // 3. Configure client
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_info = {"async-example", "1.0.0"};
    
    // 4. Create client
    AsyncMcpClient client(std::move(transport), client_config);
    
    // 5. Register notification handlers BEFORE connecting
    client.on_progress([](const ProgressNotification& progress) {
        std::cout << "[Progress] " << progress.progress;
        if (progress.total) {
            std::cout << "/" << *progress.total;
        }
        std::cout << "\n";
    });
    
    client.on_log_message([](const LoggingMessageNotification& log) {
        std::cout << "[Server Log] " << log.data.dump() << "\n";
    });
    
    // 6. Connect (auto-initializes)
    std::cout << "Connecting...\n";
    auto connect_result = co_await client.connect();
    if (!connect_result) {
        std::cerr << "ERROR: Failed to connect: " << connect_result.error().message << "\n";
        co_return 1;
    }
    
    auto& init_result = connect_result.value();
    std::cout << "Connected to: " << init_result.server_info.name;
    if (init_result.server_info.version) {
        std::cout << " v" << *init_result.server_info.version;
    }
    std::cout << "\n\n";
    
    // 7. List tools asynchronously
    std::cout << "=== Available Tools ===\n";
    auto tools = co_await client.list_tools();
    if (tools) {
        if (tools->tools.empty()) {
            std::cout << "  (no tools available)\n";
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
    
    // 8. List resources asynchronously
    std::cout << "=== Available Resources ===\n";
    auto resources = co_await client.list_resources();
    if (resources) {
        if (resources->resources.empty()) {
            std::cout << "  (no resources available)\n";
        } else {
            for (const auto& res : resources->resources) {
                std::cout << "  - " << res.uri;
                if (res.name) {
                    std::cout << " (" << *res.name << ")";
                }
                std::cout << "\n";
            }
        }
    } else {
        std::cerr << "  Failed: " << resources.error().message << "\n";
    }
    std::cout << "\n";
    
    // 9. Call a tool if available
    if (tools && !tools->tools.empty()) {
        for (const auto& tool : tools->tools) {
            if (tool.name == "list_directory") {
                std::cout << "=== Calling: list_directory ===\n";
                
                auto result = co_await client.call_tool("list_directory", {{"path", "/tmp"}});
                if (result) {
                    for (const auto& content : result->content) {
                        if (content.type == "text" && content.text) {
                            std::cout << *content.text << "\n";
                        }
                    }
                } else {
                    std::cerr << "  Failed: " << result.error().message << "\n";
                }
                std::cout << "\n";
                break;
            }
        }
    }
    
    // 10. Ping
    std::cout << "=== Ping ===\n";
    auto ping = co_await client.ping();
    if (ping) {
        std::cout << "  Pong! Server responsive.\n";
    } else {
        std::cerr << "  Failed: " << ping.error().message << "\n";
    }
    std::cout << "\n";
    
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
        
        // Spawn the main coroutine
        asio::co_spawn(io, 
            [&io, &exit_code]() -> asio::awaitable<void> {
                exit_code = co_await run_client(io);
            },
            asio::detached
        );
        
        // Run the event loop
        io.run();
        
        return exit_code;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}

