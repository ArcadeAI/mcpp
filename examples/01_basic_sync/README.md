# Example 01: Basic Synchronous Client

The simplest way to use mcpp - a blocking synchronous client with ProcessTransport.

## Flow

```
┌─────────────┐     stdio      ┌─────────────┐
│  McpClient  │◄──────────────►│  MCP Server │
│   (sync)    │   JSON-RPC     │  (process)  │
└─────────────┘                └─────────────┘
```

## What This Example Does

1. Spawns an MCP server as a subprocess
2. Connects and initializes the MCP session
3. Lists available tools
4. Calls a tool with arguments
5. Disconnects cleanly

## Code Walkthrough

```cpp
// 1. Configure the transport
ProcessTransportConfig config;
config.command = "npx";
config.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};

// 2. Create and start transport
auto transport = std::make_unique<ProcessTransport>(config);
auto start_result = transport->start();
if (!start_result) {
    std::cerr << "Failed to start: " << start_result.error().message << "\n";
    return 1;
}

// 3. Create client and connect
McpClient client(std::move(transport));
auto connect_result = client.connect();
if (!connect_result) {
    std::cerr << "Failed to connect: " << connect_result.error().message << "\n";
    return 1;
}

// 4. Initialize MCP session
auto init_result = client.initialize();
if (!init_result) {
    std::cerr << "Failed to initialize: " << init_result.error().message << "\n";
    return 1;
}

std::cout << "Connected to: " << init_result->server_info.name << "\n";

// 5. List tools
auto tools = client.list_tools();
if (tools) {
    for (const auto& tool : tools->tools) {
        std::cout << "  - " << tool.name << ": " << tool.description.value_or("") << "\n";
    }
}

// 6. Call a tool
nlohmann::json args = {{"path", "/tmp"}};
auto result = client.call_tool("list_directory", args);
if (result) {
    for (const auto& content : result->content) {
        if (content.type == "text") {
            std::cout << content.text.value_or("") << "\n";
        }
    }
}

// 7. Clean disconnect
client.disconnect();
```

## Error Handling Pattern

mcpp uses `tl::expected` for error handling - no exceptions:

```cpp
auto result = client.call_tool("my_tool", args);

if (!result) {
    // Handle error
    switch (result.error().code) {
        case ClientErrorCode::NotConnected:
            std::cerr << "Not connected to server\n";
            break;
        case ClientErrorCode::NotInitialized:
            std::cerr << "Session not initialized\n";
            break;
        case ClientErrorCode::TransportError:
            std::cerr << "Transport failed: " << result.error().message << "\n";
            break;
        case ClientErrorCode::ProtocolError:
            std::cerr << "Protocol error: " << result.error().message << "\n";
            break;
        default:
            std::cerr << "Error: " << result.error().message << "\n";
    }
    return;
}

// Success - use result.value()
auto& value = result.value();
```

## Running

```bash
# Using npx (requires Node.js)
./basic_sync_example

# Or with a Python MCP server
./basic_sync_example --command "python -m my_mcp_server"
```

