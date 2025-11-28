# Example 02: Basic Asynchronous Client

Using C++20 coroutines with `AsyncMcpClient` for non-blocking operations.

## Flow

```
┌──────────────────┐                    ┌─────────────┐
│  AsyncMcpClient  │     stdio          │  MCP Server │
│   (coroutines)   │◄──────────────────►│  (process)  │
└────────┬─────────┘     JSON-RPC       └─────────────┘
         │
         │ co_await
         ▼
┌──────────────────┐
│   asio::strand   │  ◄── Thread-safe dispatch
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│  io_context.run()│  ◄── Event loop
└──────────────────┘
```

## When to Use Async

- **High concurrency** - Many simultaneous connections
- **Non-blocking UI** - GUI applications
- **Integration** - With other async systems (web servers, etc.)
- **Resource efficiency** - Single thread handles many operations

## Code Walkthrough

```cpp
#include <mcpp/async/async_mcp_client.hpp>
#include <mcpp/async/async_process_transport.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

using namespace mcpp;
using namespace mcpp::async;

// Main coroutine
asio::awaitable<void> run_client(asio::io_context& io) {
    // 1. Configure transport
    AsyncProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
    
    // 2. Create transport (needs io_context)
    auto transport = std::make_unique<AsyncProcessTransport>(io, config);
    
    // 3. Configure client
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;  // Initialize on connect
    client_config.client_info = {"my-async-app", "1.0.0"};
    
    // 4. Create client
    AsyncMcpClient client(std::move(transport), client_config);
    
    // 5. Connect (and auto-initialize)
    auto connect_result = co_await client.connect();
    if (!connect_result) {
        std::cerr << "Connect failed: " << connect_result.error().message << "\n";
        co_return;
    }
    
    // 6. Use the client - all operations are co_await
    auto tools = co_await client.list_tools();
    if (tools) {
        for (const auto& tool : tools->tools) {
            std::cout << "Tool: " << tool.name << "\n";
        }
    }
    
    // 7. Call tool asynchronously
    auto result = co_await client.call_tool("list_directory", {{"path", "/tmp"}});
    
    // 8. Disconnect
    co_await client.disconnect();
}

int main() {
    asio::io_context io;
    
    // Spawn the coroutine
    asio::co_spawn(io, run_client(io), asio::detached);
    
    // Run the event loop
    io.run();
    
    return 0;
}
```

## Parallel Operations

With async, you can run multiple operations concurrently:

```cpp
asio::awaitable<void> parallel_example(AsyncMcpClient& client) {
    // Start multiple operations
    auto tools_future = client.list_tools();
    auto resources_future = client.list_resources();
    auto prompts_future = client.list_prompts();
    
    // Wait for all (simplified - real code needs proper gathering)
    auto tools = co_await tools_future;
    auto resources = co_await resources_future;
    auto prompts = co_await prompts_future;
}
```

## Notification Handlers

Register callbacks for server notifications:

```cpp
// Progress updates
client.on_progress([](const ProgressNotification& progress) {
    std::cout << "Progress: " << progress.progress << "/" << progress.total.value_or(100) << "\n";
});

// Resource changes
client.on_resource_updated([](const std::string& uri) {
    std::cout << "Resource updated: " << uri << "\n";
});

// Logging from server
client.on_log_message([](const LoggingMessageNotification& log) {
    std::cout << "[" << to_string(log.level) << "] " << log.data.dump() << "\n";
});
```

## Running

```bash
./basic_async_example
```

