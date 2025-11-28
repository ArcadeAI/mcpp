# mcpp Examples

This directory contains example applications demonstrating different use cases for the mcpp library.

## Examples Overview

| Example | Description | Transport | Client |
|---------|-------------|-----------|--------|
| [01_basic_sync](01_basic_sync/) | Basic synchronous client usage | Process | McpClient |
| [02_basic_async](02_basic_async/) | Basic async/coroutine client usage | Process | AsyncMcpClient |
| [03_filesystem_server](03_filesystem_server/) | Interact with filesystem MCP server | Process | McpClient |
| [04_http_transport](04_http_transport/) | Connect to remote HTTP MCP server | HTTP/SSE | McpClient |
| [05_custom_handlers](05_custom_handlers/) | Implement elicitation/sampling handlers | Process | McpClient |
| [06_circuit_breaker](06_circuit_breaker/) | Resilience with circuit breaker | Process | McpClient |
| [07_arcade_toolkit](07_arcade_toolkit/) | Arcade AI toolkit integration | Process | AsyncMcpClient |

## Building Examples

```bash
cd mcpp/build
cmake .. -DBUILD_EXAMPLES=ON
make examples
```

## Running

Each example has its own README with specific instructions. Generally:

```bash
./build/examples/01_basic_sync/basic_sync_example
```

