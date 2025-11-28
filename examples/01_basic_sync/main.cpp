// Example 01: Basic Synchronous MCP Client
// 
// Demonstrates the simplest usage pattern: blocking calls with ProcessTransport.

#include <mcpp/transport/process_transport.hpp>
#include <mcpp/client/mcp_client.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace mcpp;
using Json = nlohmann::json;

int main(int argc, char* argv[]) {
    // Default to filesystem server, can override with --command
    std::string command = "npx";
    std::vector<std::string> args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
    
    // Simple arg parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--command" && i + 1 < argc) {
            // Parse command string into command and args
            std::string cmd_str = argv[++i];
            // Split on first space
            auto space_pos = cmd_str.find(' ');
            if (space_pos != std::string::npos) {
                command = cmd_str.substr(0, space_pos);
                // Rest becomes single arg (simplified)
                args = {cmd_str.substr(space_pos + 1)};
            } else {
                command = cmd_str;
                args.clear();
            }
        }
    }
    
    std::cout << "=== Basic Synchronous MCP Client Example ===\n\n";
    
    // 1. Configure transport
    ProcessTransportConfig config;
    config.command = command;
    config.args = args;
    config.use_content_length_framing = false;  // Most servers use raw JSON
    
    std::cout << "Starting server: " << command;
    for (const auto& a : args) std::cout << " " << a;
    std::cout << "\n\n";
    
    // 2. Create and start transport
    auto transport = std::make_unique<ProcessTransport>(config);
    auto start_result = transport->start();
    if (!start_result) {
        std::cerr << "ERROR: Failed to start server: " << start_result.error().message << "\n";
        return 1;
    }
    
    // 3. Create client
    McpClient client(std::move(transport));
    
    // 4. Connect
    auto connect_result = client.connect();
    if (!connect_result) {
        std::cerr << "ERROR: Failed to connect: " << connect_result.error().message << "\n";
        return 1;
    }
    std::cout << "Connected!\n";
    
    // 5. Initialize MCP session
    auto init_result = client.initialize();
    if (!init_result) {
        std::cerr << "ERROR: Failed to initialize: " << init_result.error().message << "\n";
        return 1;
    }
    
    std::cout << "Server: " << init_result->server_info.name;
    if (init_result->server_info.version) {
        std::cout << " v" << *init_result->server_info.version;
    }
    std::cout << "\n";
    std::cout << "Protocol: " << init_result->protocol_version << "\n\n";
    
    // 6. List available tools
    std::cout << "=== Available Tools ===\n";
    auto tools = client.list_tools();
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
        std::cerr << "  Failed to list tools: " << tools.error().message << "\n";
    }
    std::cout << "\n";
    
    // 7. List available resources
    std::cout << "=== Available Resources ===\n";
    auto resources = client.list_resources();
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
        std::cerr << "  Failed to list resources: " << resources.error().message << "\n";
    }
    std::cout << "\n";
    
    // 8. Try calling a tool (if list_directory exists)
    if (tools && !tools->tools.empty()) {
        // Find a safe tool to call
        for (const auto& tool : tools->tools) {
            if (tool.name == "list_directory" || tool.name == "read_file") {
                std::cout << "=== Calling Tool: " << tool.name << " ===\n";
                
                Json tool_args;
                if (tool.name == "list_directory") {
                    tool_args = {{"path", "/tmp"}};
                } else {
                    tool_args = {{"path", "/etc/hostname"}};
                }
                
                auto result = client.call_tool(tool.name, tool_args);
                if (result) {
                    for (const auto& content : result->content) {
                        if (content.type == "text" && content.text) {
                            std::cout << *content.text << "\n";
                        }
                    }
                } else {
                    std::cerr << "  Tool call failed: " << result.error().message << "\n";
                }
                std::cout << "\n";
                break;
            }
        }
    }
    
    // 9. Ping the server
    std::cout << "=== Ping ===\n";
    auto ping_result = client.ping();
    if (ping_result) {
        std::cout << "  Pong! Server is responsive.\n";
    } else {
        std::cerr << "  Ping failed: " << ping_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 10. Clean disconnect
    std::cout << "Disconnecting...\n";
    client.disconnect();
    std::cout << "Done!\n";
    
    return 0;
}

