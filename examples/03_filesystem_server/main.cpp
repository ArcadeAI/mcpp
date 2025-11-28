// Example 03: Filesystem MCP Server
// 
// Complete example of file operations using the official MCP filesystem server.

#include <mcpp/transport/process_transport.hpp>
#include <mcpp/client/mcp_client.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <fstream>

using namespace mcpp;
using Json = nlohmann::json;

void print_tool_result(const CallToolResult& result) {
    for (const auto& content : result.content) {
        if (content.type == "text" && content.text) {
            std::cout << *content.text << "\n";
        }
    }
}

int main() {
    std::cout << "=== Filesystem MCP Server Example ===\n\n";
    
    // Create a test file
    {
        std::ofstream test_file("/tmp/mcpp_test.txt");
        test_file << "Hello from mcpp!\nThis is a test file.\n";
    }
    
    // 1. Configure transport for filesystem server
    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
    
    std::cout << "Starting filesystem server for /tmp...\n\n";
    
    // 2. Create and start
    auto transport = std::make_unique<ProcessTransport>(config);
    if (!transport->start()) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    
    McpClient client(std::move(transport));
    
    if (!client.connect()) {
        std::cerr << "Failed to connect\n";
        return 1;
    }
    
    auto init = client.initialize();
    if (!init) {
        std::cerr << "Failed to initialize: " << init.error().message << "\n";
        return 1;
    }
    
    std::cout << "Connected to: " << init->server_info.name << "\n\n";
    
    // 3. List available tools
    std::cout << "=== Available Tools ===\n";
    auto tools = client.list_tools();
    if (tools) {
        for (const auto& tool : tools->tools) {
            std::cout << "  " << tool.name;
            if (tool.description) {
                std::cout << " - " << *tool.description;
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
    
    // 4. List directory
    std::cout << "=== list_directory /tmp ===\n";
    auto list_result = client.call_tool("list_directory", {{"path", "/tmp"}});
    if (list_result) {
        print_tool_result(*list_result);
    } else {
        std::cerr << "Error: " << list_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 5. Read our test file
    std::cout << "=== read_file /tmp/mcpp_test.txt ===\n";
    auto read_result = client.call_tool("read_file", {{"path", "/tmp/mcpp_test.txt"}});
    if (read_result) {
        print_tool_result(*read_result);
    } else {
        std::cerr << "Error: " << read_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 6. Write a new file
    std::cout << "=== write_file /tmp/mcpp_output.txt ===\n";
    auto write_result = client.call_tool("write_file", {
        {"path", "/tmp/mcpp_output.txt"},
        {"content", "Written by mcpp example!\nTimestamp: " + std::to_string(time(nullptr))}
    });
    if (write_result) {
        std::cout << "File written successfully!\n";
        print_tool_result(*write_result);
    } else {
        std::cerr << "Error: " << write_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 7. Get file info
    std::cout << "=== get_file_info /tmp/mcpp_output.txt ===\n";
    auto info_result = client.call_tool("get_file_info", {{"path", "/tmp/mcpp_output.txt"}});
    if (info_result) {
        print_tool_result(*info_result);
    } else {
        std::cerr << "Error: " << info_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 8. Search for files
    std::cout << "=== search_files /tmp/*.txt ===\n";
    auto search_result = client.call_tool("search_files", {
        {"path", "/tmp"},
        {"pattern", "mcpp*.txt"}
    });
    if (search_result) {
        print_tool_result(*search_result);
    } else {
        std::cerr << "Error: " << search_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 9. Create a directory
    std::cout << "=== create_directory /tmp/mcpp_example_dir ===\n";
    auto mkdir_result = client.call_tool("create_directory", {{"path", "/tmp/mcpp_example_dir"}});
    if (mkdir_result) {
        std::cout << "Directory created!\n";
    } else {
        std::cerr << "Error (may already exist): " << mkdir_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 10. Move/rename a file
    std::cout << "=== move_file /tmp/mcpp_output.txt -> /tmp/mcpp_example_dir/moved.txt ===\n";
    auto move_result = client.call_tool("move_file", {
        {"source", "/tmp/mcpp_output.txt"},
        {"destination", "/tmp/mcpp_example_dir/moved.txt"}
    });
    if (move_result) {
        std::cout << "File moved!\n";
    } else {
        std::cerr << "Error: " << move_result.error().message << "\n";
    }
    std::cout << "\n";
    
    // 11. Read via resource URI (if supported)
    std::cout << "=== Read via resource URI ===\n";
    auto resources = client.list_resources();
    if (resources && !resources->resources.empty()) {
        std::cout << "Resources available:\n";
        for (const auto& res : resources->resources) {
            std::cout << "  - " << res.uri << "\n";
        }
        
        // Try to read first resource
        if (!resources->resources.empty()) {
            auto content = client.read_resource(resources->resources[0].uri);
            if (content) {
                std::cout << "\nContent of " << resources->resources[0].uri << ":\n";
                for (const auto& c : content->contents) {
                    if (c.is_text()) {
                        std::cout << c.as_text().text << "\n";
                    }
                }
            }
        }
    } else {
        std::cout << "  (No resources exposed by this server)\n";
    }
    std::cout << "\n";
    
    // Cleanup
    std::cout << "Disconnecting...\n";
    client.disconnect();
    std::cout << "Done!\n";
    
    return 0;
}

