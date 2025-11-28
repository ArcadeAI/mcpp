# Example 04: HTTP Transport

Connect to remote MCP servers over HTTP with Server-Sent Events (SSE).

## Architecture

```
┌─────────────────┐                         ┌─────────────────┐
│    McpClient    │                         │   Remote MCP    │
│                 │                         │     Server      │
├─────────────────┤                         ├─────────────────┤
│  HttpTransport  │                         │   HTTP Server   │
│                 │                         │                 │
│  ┌───────────┐  │    POST /mcp/message    │  ┌───────────┐  │
│  │  Request  │──┼────────────────────────►│  │  Handler  │  │
│  │  Sender   │  │    JSON-RPC Request     │  │           │  │
│  └───────────┘  │                         │  └───────────┘  │
│                 │                         │                 │
│  ┌───────────┐  │    GET /mcp/sse         │  ┌───────────┐  │
│  │    SSE    │◄─┼────────────────────────►│  │    SSE    │  │
│  │  Reader   │  │   Server-Sent Events    │  │  Emitter  │  │
│  └───────────┘  │   (responses, notifs)   │  └───────────┘  │
└─────────────────┘                         └─────────────────┘
```

## Session Management

HTTP transport uses session IDs for stateful connections:

```
1. Client connects to SSE endpoint
2. Server sends session ID in Mcp-Session-Id header
3. Client includes session ID in all subsequent POST requests
4. If session expires (404), client reconnects automatically
```

## Configuration Options

```cpp
HttpClientConfig config;

// Required
config.base_url = "https://api.example.com/mcp/";

// Authentication
config.headers = {
    {"Authorization", "Bearer YOUR_TOKEN"},
    {"X-API-Key", "your-api-key"}
};

// Timeouts
config.timeout = std::chrono::seconds(30);
config.connect_timeout = std::chrono::seconds(10);

// SSE settings
config.sse_endpoint = "/sse";      // Default: /sse
config.message_endpoint = "/message";  // Default: /message

// Retry settings
config.max_retries = 3;
config.retry_delay = std::chrono::seconds(1);
```

## Code Example

```cpp
#include <mcpp/transport/http_transport.hpp>
#include <mcpp/client/mcp_client.hpp>

using namespace mcpp;

int main() {
    // Configure HTTP transport
    HttpClientConfig config;
    config.base_url = "https://api.example.com/mcp/";
    config.headers = {{"Authorization", "Bearer token123"}};
    config.timeout = std::chrono::seconds(30);
    
    // Create transport
    auto transport = std::make_unique<HttpTransport>(config);
    
    // Create client with circuit breaker for resilience
    McpClientConfig client_config;
    client_config.enable_circuit_breaker = true;
    client_config.circuit_breaker.failure_threshold = 5;
    client_config.circuit_breaker.recovery_timeout = std::chrono::seconds(30);
    
    McpClient client(std::move(transport), client_config);
    
    // Connect (establishes SSE connection)
    auto connect_result = client.connect();
    if (!connect_result) {
        std::cerr << "Failed: " << connect_result.error().message << "\n";
        return 1;
    }
    
    // Initialize
    auto init = client.initialize();
    
    // Use normally
    auto tools = client.list_tools();
    
    client.disconnect();
    return 0;
}
```

## Error Handling

HTTP-specific errors:

```cpp
auto result = client.call_tool("my_tool", args);
if (!result) {
    auto& error = result.error();
    
    if (error.code == ClientErrorCode::TransportError) {
        // Network error, timeout, etc.
        std::cerr << "Transport error: " << error.message << "\n";
        
        // Check if circuit breaker is open
        if (client.is_circuit_open()) {
            std::cerr << "Circuit breaker open - server unavailable\n";
        }
    }
}
```

## Running

```bash
# Set your server URL and token
export MCP_SERVER_URL="https://api.example.com/mcp/"
export MCP_TOKEN="your-token"

./http_transport_example
```

## Supported Servers

Any MCP server implementing HTTP+SSE transport:

- GitHub Copilot MCP (`https://api.githubcopilot.com/mcp/`)
- Custom enterprise MCP servers
- Self-hosted MCP servers with HTTP transport

