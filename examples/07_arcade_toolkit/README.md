# Example 07: Arcade AI MCP Gateway Integration

Connect to Arcade AI MCP gateways for enterprise AI tool orchestration.

## Two Connection Methods

### 1. HTTP Gateway (Recommended for Production)

Arcade gateways handle authentication and secrets server-side. You only need:
- Your Arcade API key
- Your user ID
- The gateway slug

**No local secrets or tool packages needed - Arcade manages everything!**

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Your C++ Application                             │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                      McpClient / CLI                               │ │
│  │                                                                     │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │ │
│  │  │ Tools    │  │ Resources│  │ Progress │  │ Circuit Breaker  │   │ │
│  │  │ API      │  │ API      │  │ Tracking │  │ (resilience)     │   │ │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────────────┘   │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                    │                                     │
│                          HttpTransport                                   │
└────────────────────────────────────┼─────────────────────────────────────┘
                                     │ HTTPS
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     https://api.arcade.dev/mcp/<gateway>                │
│                                                                          │
│  Headers:                                                                │
│    Authorization: Bearer arc_xxx                                        │
│    Arcade-User-ID: user@example.com                                     │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐│
│  │ Arcade handles:                                                      ││
│  │   - OAuth tokens for GitHub, Slack, etc.                            ││
│  │   - API key management                                               ││
│  │   - Rate limiting and quotas                                         ││
│  │   - Tool versioning                                                  ││
│  └─────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────┘
```

### 2. Stdio (Local Development)

For local development or self-hosted toolkits:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Your C++ Application                             │
│                                                                          │
│                     AsyncProcessTransport                                │
└────────────────────────────────────┼─────────────────────────────────────┘
                                     │ stdin/stdout
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        arcade_mcp_server                                 │
│                                                                          │
│  python -m arcade_mcp_server stdio --tool-package <toolkit>             │
│                                                                          │
│  Requires local secrets: GITHUB_TOKEN, SLACK_TOKEN, etc.                │
└─────────────────────────────────────────────────────────────────────────┘
```

## CLI Usage

### HTTP Gateway (Recommended)

```bash
# With explicit credentials
mcpp-cli --arcade my-gateway \
         --arcade-key arc_xxx \
         --arcade-user user@example.com \
         --list-tools

# With environment variables
export ARCADE_API_KEY=arc_xxx
export ARCADE_USER_ID=user@example.com
mcpp-cli --arcade my-gateway --list-tools

# Call a tool
mcpp-cli --arcade my-gateway --call-tool Github_WhoAmI

# Search repositories
mcpp-cli --arcade my-gateway \
         --call-tool Github_SearchMyRepos \
         --tool-args '{"repo_name": "mcpp"}'

# Interactive mode
mcpp-cli --arcade my-gateway --interactive
```

### Generic HTTP (any MCP server)

```bash
mcpp-cli --url https://example.com/mcp \
         --bearer "secret-token" \
         --header "X-Custom: value" \
         --list-tools
```

### Stdio (local server)

```bash
mcpp-cli -c 'npx' -a '-y' -a '@modelcontextprotocol/server-filesystem' -a '/tmp' --list-tools
```

## Code Example (HTTP)

```cpp
#include <mcpp/transport/http_transport.hpp>
#include <mcpp/protocol/mcp_types.hpp>

int main() {
    // Configure HTTP transport for Arcade gateway
    HttpTransportConfig config;
    config.base_url = "https://api.arcade.dev/mcp/my-gateway";
    
    // Arcade authentication headers
    config.with_bearer_token("arc_xxx");
    config.with_header("Arcade-User-ID", "user@example.com");
    
    // Disable SSE for simple request-response
    config.auto_open_sse_stream = false;
    
    HttpTransport transport(config);
    transport.start();
    
    // ... use transport with MCP client ...
    
    transport.stop();
}
```

## Tool Naming Convention

Arcade tools follow the pattern: `ToolkitName_ToolName`

```
Github_WhoAmI           → Get authenticated user info
Github_SearchMyRepos    → Search repositories
Github_CreateIssue      → Create GitHub issue
Slack_SendMessage       → Send Slack message
Linear_CreateIssue      → Create Linear issue
```

## Key Differences: HTTP vs Stdio

| Feature | HTTP Gateway | Stdio |
|---------|-------------|-------|
| Secrets | Managed by Arcade | Local env vars |
| Setup | Just API key | Install toolkit |
| OAuth | Arcade handles | Manual setup |
| Scaling | Cloud-native | Single process |
| Use case | Production | Development |

## Error Handling

```cpp
auto result = client.call_tool("Github_CreateIssue", args);

if (!result) {
    std::cerr << "Error: " << result.error().message << "\n";
    // Common errors:
    // - 401: Invalid API key
    // - 403: User not authorized for this tool
    // - 404: Gateway not found
    // - 422: Invalid tool arguments
}
```

## Enterprise Use Cases

1. **Trading Systems**: Call market data tools with low latency
2. **DevOps Automation**: GitHub/GitLab operations from C++ services
3. **Data Pipelines**: Integrate with Google Sheets, databases
4. **Alerting**: Send Slack/Teams notifications from C++ backends

## More Information

- [Arcade Documentation](https://docs.arcade.dev/en/home/build-tools/call-tools-from-mcp-clients)
- [MCP Specification](https://modelcontextprotocol.io)
