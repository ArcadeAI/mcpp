# Example 03: Filesystem MCP Server

Complete example of interacting with the official MCP filesystem server.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Your Application                      │
├─────────────────────────────────────────────────────────────┤
│                         McpClient                            │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────────────┐ │
│  │ Tools   │  │Resources│  │ Prompts │  │ Resource        │ │
│  │ API     │  │ API     │  │ API     │  │ Subscriptions   │ │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────────┬────────┘ │
├───────┼────────────┼────────────┼────────────────┼──────────┤
│       └────────────┴────────────┴────────────────┘          │
│                    ProcessTransport                          │
│                      stdin/stdout                            │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│              @modelcontextprotocol/server-filesystem         │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ Tools:                                                   ││
│  │   - read_file      Read file contents                   ││
│  │   - write_file     Write to a file                      ││
│  │   - list_directory List directory contents              ││
│  │   - create_directory Create a new directory             ││
│  │   - move_file      Move/rename a file                   ││
│  │   - search_files   Search for files by pattern          ││
│  │   - get_file_info  Get file metadata                    ││
│  └─────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────┐│
│  │ Resources:                                               ││
│  │   - file://{path}  Direct file access                   ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

## Available Operations

### Tools

| Tool | Arguments | Description |
|------|-----------|-------------|
| `read_file` | `path: string` | Read entire file contents |
| `write_file` | `path: string, content: string` | Write content to file |
| `list_directory` | `path: string` | List files and directories |
| `create_directory` | `path: string` | Create new directory |
| `move_file` | `source: string, destination: string` | Move or rename file |
| `search_files` | `path: string, pattern: string` | Search files by glob pattern |
| `get_file_info` | `path: string` | Get file metadata (size, modified, etc.) |

### Resources

Files are exposed as resources with `file://` URIs:

```cpp
auto content = client.read_resource("file:///tmp/myfile.txt");
```

## Code Example

```cpp
// List directory
auto result = client.call_tool("list_directory", {{"path", "/tmp"}});

// Read a file
auto content = client.call_tool("read_file", {{"path", "/tmp/test.txt"}});

// Write a file
auto write_result = client.call_tool("write_file", {
    {"path", "/tmp/output.txt"},
    {"content", "Hello from mcpp!"}
});

// Search for files
auto search = client.call_tool("search_files", {
    {"path", "/tmp"},
    {"pattern", "*.txt"}
});

// Get file info
auto info = client.call_tool("get_file_info", {{"path", "/tmp/test.txt"}});
```

## Security Note

The filesystem server restricts access to the directories you specify when starting it:

```bash
# Only /tmp is accessible
npx -y @modelcontextprotocol/server-filesystem /tmp

# Multiple directories
npx -y @modelcontextprotocol/server-filesystem /tmp /home/user/projects
```

## Running

```bash
./filesystem_example
```

