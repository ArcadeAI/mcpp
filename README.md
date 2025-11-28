# mcpp

A modern C++20 client library for the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/).

## Features

- **Full MCP Protocol Support** - Tools, resources, prompts, completions, subscriptions
- **Multiple Transports** - Process (stdio), HTTP with SSE
- **Sync & Async APIs** - Blocking `McpClient` and coroutine-based `AsyncMcpClient`
- **Production Ready** - Circuit breaker, URL validation, session management, retry logic
- **Modern C++** - C++20 coroutines, concepts, `std::expected`-style error handling
- **Comprehensive Tests** - 495+ tests covering unit, integration, and real server scenarios

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            Your Application                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   ┌─────────────────────────────┐    ┌─────────────────────────────┐       │
│   │        McpClient            │    │      AsyncMcpClient         │       │
│   │      (synchronous)          │    │      (coroutines)           │       │
│   └──────────────┬──────────────┘    └──────────────┬──────────────┘       │
│                  │                                   │                       │
│   ┌──────────────┴───────────────────────────────────┴──────────────┐      │
│   │                        Transport Layer                           │      │
│   │  ┌─────────────────────┐        ┌─────────────────────┐         │      │
│   │  │  ProcessTransport   │        │    HttpTransport    │         │      │
│   │  │  (stdio to subprocess)       │   (HTTP + SSE)      │         │      │
│   │  └─────────────────────┘        └─────────────────────┘         │      │
│   └──────────────────────────────────────────────────────────────────┘      │
│                                                                              │
│   ┌──────────────────────────────────────────────────────────────────┐      │
│   │                      Resilience & Security                        │      │
│   │  ┌───────────────┐  ┌───────────────┐  ┌───────────────────┐    │      │
│   │  │Circuit Breaker│  │ URL Validator │  │ Session Manager   │    │      │
│   │  └───────────────┘  └───────────────┘  └───────────────────┘    │      │
│   └──────────────────────────────────────────────────────────────────┘      │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MCP Server                                      │
│  (filesystem, GitHub, Slack, custom AI agents, etc.)                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Quick Start

### Requirements

- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.20+

### Building

```bash
git clone https://github.com/your-org/mcpp.git
cd mcpp
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Basic Usage

```cpp
#include <mcpp/transport/process_transport.hpp>
#include <mcpp/client/mcp_client.hpp>

using namespace mcpp;

int main() {
    // Connect to an MCP server via stdio
    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
    
    auto transport = std::make_unique<ProcessTransport>(config);
    transport->start();
    
    McpClient client(std::move(transport));
    client.connect();
    client.initialize();
    
    // List available tools
    auto tools = client.list_tools();
    if (tools) {
        for (const auto& tool : tools->tools) {
            std::cout << "Tool: " << tool.name << "\n";
        }
    }
    
    // Call a tool
    auto result = client.call_tool("read_file", {{"path", "/tmp/test.txt"}});
    if (result) {
        for (const auto& content : result->content) {
            if (content.type == "text") {
                std::cout << content.text.value_or("") << "\n";
            }
        }
    }
    
    client.disconnect();
    return 0;
}
```

## Request/Response Flow

```
┌────────────┐                                    ┌────────────┐
│   Client   │                                    │   Server   │
└─────┬──────┘                                    └─────┬──────┘
      │                                                 │
      │  ──────────── initialize ──────────────────►   │
      │  ◄─────────── InitializeResult ────────────    │
      │                                                 │
      │  ──────────── notifications/initialized ───►   │
      │                                                 │
      │  ──────────── tools/list ──────────────────►   │
      │  ◄─────────── ListToolsResult ─────────────    │
      │                                                 │
      │  ──────────── tools/call ──────────────────►   │
      │  ◄─────────── CallToolResult ──────────────    │
      │                                                 │
      │  ◄─────────── notifications/progress ──────    │  (optional)
      │                                                 │
      │  ──────────── ping ────────────────────────►   │
      │  ◄─────────── pong ────────────────────────    │
      │                                                 │
      ▼                                                 ▼
```

## Examples

See the [`examples/`](examples/) directory for complete working examples:

| Example | Description |
|---------|-------------|
| [01_basic_sync](examples/01_basic_sync/) | Synchronous client basics |
| [02_basic_async](examples/02_basic_async/) | Async/coroutine client |
| [03_filesystem_server](examples/03_filesystem_server/) | File operations |
| [04_http_transport](examples/04_http_transport/) | Remote HTTP servers |
| [05_custom_handlers](examples/05_custom_handlers/) | Elicitation, sampling, roots |
| [06_circuit_breaker](examples/06_circuit_breaker/) | Resilience patterns |
| [07_arcade_toolkit](examples/07_arcade_toolkit/) | Arcade AI integration |

## MCP Protocol Coverage

| Feature | Status | Description |
|---------|--------|-------------|
| Initialize/Shutdown | ✅ | Session lifecycle |
| Tools (list, call) | ✅ | Execute server-provided tools |
| Resources (list, read, subscribe) | ✅ | Access server resources |
| Resource Templates | ✅ | URI template discovery |
| Prompts (list, get) | ✅ | Prompt templates |
| Completions | ✅ | Autocompletion API |
| Ping | ✅ | Health checks |
| Cancellation | ✅ | Cancel in-progress operations |
| Progress Notifications | ✅ | Track long-running ops |
| Logging Control | ✅ | Server log level |
| Elicitation Handler | ✅ | User input requests |
| Sampling Handler | ✅ | LLM completion requests |
| Roots Handler | ✅ | Filesystem root exposure |

## Transports

### ProcessTransport (stdio)

```
┌──────────────┐     stdin      ┌──────────────┐
│    Client    │───────────────►│    Server    │
│              │◄───────────────│   (process)  │
└──────────────┘     stdout     └──────────────┘
```

```cpp
ProcessTransportConfig config;
config.command = "python";
config.args = {"-m", "my_mcp_server"};
config.use_content_length_framing = false;  // or true for framed mode
config.working_directory = "/path/to/server";
config.environment = {{"API_KEY", "secret"}};
```

### HttpTransport (HTTP + SSE)

```
┌──────────────┐   POST /message   ┌──────────────┐
│    Client    │──────────────────►│    Server    │
│              │◄──────────────────│    (HTTP)    │
└──────────────┘   SSE /sse        └──────────────┘
```

```cpp
HttpClientConfig config;
config.base_url = "https://api.example.com/mcp/";
config.headers = {{"Authorization", "Bearer token"}};
config.timeout = std::chrono::seconds(30);
```

## Error Handling

Uses `tl::expected` for explicit error handling without exceptions:

```cpp
auto result = client.call_tool("my_tool", args);
if (!result) {
    switch (result.error().code) {
        case ClientErrorCode::NotConnected:
            // Handle connection error
            break;
        case ClientErrorCode::TransportError:
            // Handle transport failure
            break;
        case ClientErrorCode::ProtocolError:
            // Handle protocol error
            break;
    }
    return;
}
// Use result.value()
```

## Circuit Breaker

```
     CLOSED ──────► OPEN ──────► HALF-OPEN
        ▲            │               │
        │            │   success     │
        └────────────┴───────────────┘
              failure: back to OPEN
```

```cpp
McpClientConfig config;
config.enable_circuit_breaker = true;
config.circuit_breaker.failure_threshold = 5;
config.circuit_breaker.recovery_timeout = std::chrono::seconds(30);

McpClient client(std::move(transport), config);

if (client.is_circuit_open()) {
    // Fast-fail, don't wait for timeout
}
```

## CLI Tool

```bash
# List tools
./mcpp-cli -c 'npx' -a '-y' -a '@modelcontextprotocol/server-filesystem' -a '/tmp' --list-tools

# Call a tool
./mcpp-cli -c 'python' -a 'server.py' --call-tool read_file --tool-args '{"path":"/tmp/test.txt"}'

# Interactive mode
./mcpp-cli -c 'node' -a 'server.js' --interactive

# JSON output
./mcpp-cli -c 'python' -a 'server.py' --list-tools --json
```

## Project Structure

```
mcpp/
├── include/mcpp/
│   ├── protocol/      # MCP types and JSON serialization
│   ├── transport/     # Transport layer (Process, HTTP/SSE)
│   ├── client/        # Synchronous McpClient
│   ├── async/         # Asynchronous AsyncMcpClient
│   ├── resilience/    # Circuit breaker
│   ├── security/      # URL validation
│   └── log/           # Logging interfaces
├── src/               # Implementation files
├── tests/             # Test suite (495+ tests)
├── tools/mcpp-cli/    # Command-line interface
└── examples/          # Usage examples
```

## Dependencies

All fetched automatically via CMake FetchContent:

- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing
- [ASIO](https://think-async.com/Asio/) - Async I/O and coroutines
- [tl::expected](https://github.com/TartanLlama/expected) - Error handling
- [ada-url](https://github.com/nickvidal/ada) - URL parsing
- [spdlog](https://github.com/gabime/spdlog) - Logging
- [cpr](https://github.com/libcpr/cpr) - HTTP client
- [Catch2](https://github.com/catchorg/Catch2) - Testing
- [cxxopts](https://github.com/jarro2783/cxxopts) - CLI parsing

## Running Tests

```bash
cd build
ctest --output-on-failure

# Or directly
./mcpp_tests                    # All tests
./mcpp_tests "[mcp_client]"     # Client tests
./mcpp_tests "[real-server]"    # Integration tests
```

## License

MIT License - see [LICENSE](LICENSE) file.
