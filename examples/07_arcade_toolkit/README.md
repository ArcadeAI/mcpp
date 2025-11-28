# Example 07: Arcade AI Toolkit Integration

Connect to Arcade AI MCP servers for enterprise AI tool orchestration.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Your C++ Application                             │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                      AsyncMcpClient                                 │ │
│  │                                                                     │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │ │
│  │  │ Tools    │  │ Resources│  │ Progress │  │ Circuit Breaker  │   │ │
│  │  │ API      │  │ API      │  │ Tracking │  │ (resilience)     │   │ │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘   │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                    │                                     │
│                     AsyncProcessTransport                                │
└────────────────────────────────────┼─────────────────────────────────────┘
                                     │ stdin/stdout
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        arcade_mcp_server                                 │
│                                                                          │
│  python -m arcade_mcp_server stdio --tool-package <toolkit>             │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │ Available Toolkits:                                                  ││
│  │   - github      GitHub API (repos, issues, PRs, users)              ││
│  │   - slack       Slack messaging and channels                         ││
│  │   - clickup     Project management                                   ││
│  │   - google      Google Workspace (Drive, Docs, Sheets)              ││
│  │   - ...         Many more enterprise integrations                    ││
│  └─────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

## Tool Naming Convention

Arcade tools follow the pattern: `ToolkitName_ToolName`

```
github_get_me           → Get authenticated user info
github_search_repos     → Search repositories
slack_send_message      → Send Slack message
clickup_create_task     → Create ClickUp task
```

## Environment Setup

```bash
# GitHub toolkit
export GITHUB_PERSONAL_ACCESS_TOKEN="ghp_xxxx"

# Slack toolkit
export SLACK_BOT_TOKEN="xoxb-xxxx"

# Other toolkits may need their own tokens
```

## Code Example

```cpp
#include <mcpp/async/async_mcp_client.hpp>
#include <mcpp/async/async_process_transport.hpp>

asio::awaitable<void> use_github_toolkit(asio::io_context& io) {
    // Configure for Arcade GitHub toolkit
    AsyncProcessTransportConfig config;
    config.command = "python";
    config.args = {"-m", "arcade_mcp_server", "stdio", 
                   "--tool-package", "github"};
    config.working_directory = "/path/to/arcade/toolkits/github";
    config.environment = {
        {"GITHUB_PERSONAL_ACCESS_TOKEN", std::getenv("GITHUB_PERSONAL_ACCESS_TOKEN")}
    };
    
    auto transport = std::make_unique<AsyncProcessTransport>(io, config);
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    
    AsyncMcpClient client(std::move(transport), client_config);
    
    co_await client.connect();
    
    // List available GitHub tools
    auto tools = co_await client.list_tools();
    // Tools: github_get_me, github_search_repos, github_create_issue, etc.
    
    // Get authenticated user
    auto me = co_await client.call_tool("github_get_me", {});
    
    // Search repositories
    auto repos = co_await client.call_tool("github_search_repos", {
        {"query", "language:cpp stars:>1000"}
    });
    
    co_await client.disconnect();
}
```

## Using Index Tool

Many Arcade toolkits have an Index tool for discovery:

```cpp
// Call the index tool first to see all available tools
auto index = co_await client.call_tool("Github_Index", {});
// Returns detailed info about all tools in the toolkit
```

## Progress Tracking

For long-running operations:

```cpp
client.on_progress([](const ProgressNotification& p) {
    std::cout << "Progress: " << p.progress;
    if (p.total) std::cout << "/" << *p.total;
    std::cout << "\n";
});

// Call with progress token
RequestMeta meta;
meta.progress_token = "my-operation-123";

auto result = co_await client.call_tool("github_search_repos", 
    {{"query", "stars:>10000"}}, meta);
```

## Error Handling

```cpp
auto result = co_await client.call_tool("github_create_issue", args);

if (!result) {
    if (result.error().code == ClientErrorCode::ProtocolError) {
        // Tool returned an error (e.g., permission denied)
        if (result.error().rpc_error) {
            std::cerr << "Tool error: " << result.error().rpc_error->message << "\n";
        }
    } else if (result.error().code == ClientErrorCode::TransportError) {
        // Connection issue
        if (client.is_circuit_open()) {
            std::cerr << "Server unavailable\n";
        }
    }
}
```

## Running

```bash
# Set your GitHub token
export GITHUB_PERSONAL_ACCESS_TOKEN="your_token"

# Run the example
./arcade_toolkit_example
```

## Enterprise Use Cases

1. **Trading Systems**: Call market data tools with low latency
2. **DevOps Automation**: GitHub/GitLab operations from C++ services
3. **Data Pipelines**: Integrate with Google Sheets, databases
4. **Alerting**: Send Slack/Teams notifications from C++ backends

