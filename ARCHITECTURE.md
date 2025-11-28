# MCPP Architecture Documentation

> **Model Context Protocol (MCP) Client Library in Modern C++**  
> A comprehensive, production-ready MCP client implementation featuring coroutines, circuit breakers, and modular transport layers.

---

## Table of Contents

1. [High-Level Architecture](#high-level-architecture)
2. [Component Relationships](#component-relationships)
3. [Data Flow Diagrams](#data-flow-diagrams)
4. [Layer Architecture](#layer-architecture)
5. [Module Breakdown](#module-breakdown)
   - [Client Layer](#client-layer)
   - [Transport Layer](#transport-layer)
   - [Protocol Layer](#protocol-layer)
   - [Resilience Layer](#resilience-layer)
   - [Security Layer](#security-layer)
   - [Logging Layer](#logging-layer)
   - [Async Layer](#async-layer)
6. [File-by-File Documentation](#file-by-file-documentation)
7. [Key Concepts](#key-concepts)
8. [Design Patterns Used](#design-patterns-used)

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              APPLICATION LAYER                               │
│                                                                              │
│   ┌─────────────────────┐        ┌──────────────────────────┐               │
│   │     mcpp-cli        │        │    User Application      │               │
│   │  (CLI Testing Tool) │        │  (Your MCP Integration)  │               │
│   └──────────┬──────────┘        └────────────┬─────────────┘               │
└──────────────┼───────────────────────────────┼──────────────────────────────┘
               │                               │
               ▼                               ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                               CLIENT LAYER                                   │
│                                                                              │
│   ┌─────────────────────────────┐    ┌────────────────────────────────┐     │
│   │        McpClient            │    │      AsyncMcpClient            │     │
│   │    (Synchronous Client)     │    │  (C++20 Coroutine Client)      │     │
│   │                             │    │                                │     │
│   │  • connect()/disconnect()   │    │  • co_await connect()          │     │
│   │  • list_tools()             │    │  • co_await list_tools()       │     │
│   │  • call_tool()              │    │  • co_await call_tool()        │     │
│   │  • Circuit Breaker          │    │  • ASIO strand for safety      │     │
│   └──────────────┬──────────────┘    └───────────────┬────────────────┘     │
└──────────────────┼───────────────────────────────────┼──────────────────────┘
                   │                                   │
                   ▼                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                             TRANSPORT LAYER                                  │
│                                                                              │
│   ┌─────────────────────┐    ┌─────────────────────┐   ┌──────────────────┐ │
│   │   HttpTransport     │    │  ProcessTransport   │   │ AsyncProcess     │ │
│   │  (HTTP + SSE)       │    │  (stdio pipes)      │   │ Transport        │ │
│   │                     │    │                     │   │ (ASIO-based)     │ │
│   │  • POST for send    │    │  • fork() + exec()  │   │                  │ │
│   │  • SSE for receive  │    │  • Content-Length   │   │  • Awaitable     │ │
│   │  • Session mgmt     │    │    framing          │   │    send/receive  │ │
│   └─────────┬───────────┘    └──────────┬──────────┘   └────────┬─────────┘ │
└─────────────┼───────────────────────────┼──────────────────────┼────────────┘
              │                           │                      │
              ▼                           ▼                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            PROTOCOL LAYER                                    │
│                                                                              │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                         MCP Protocol Types                          │   │
│   │                                                                     │   │
│   │  ┌──────────┐  ┌───────────┐  ┌──────────┐  ┌────────────────┐    │   │
│   │  │  Tools   │  │ Resources │  │ Prompts  │  │  Capabilities  │    │   │
│   │  └──────────┘  └───────────┘  └──────────┘  └────────────────┘    │   │
│   │                                                                     │   │
│   │  ┌───────────────────────────────────────────────────────────┐     │   │
│   │  │              JSON-RPC 2.0 Messaging                       │     │   │
│   │  │  • Request (id, method, params)                           │     │   │
│   │  │  • Notification (method, params)                          │     │   │
│   │  │  • Response (id, result/error)                            │     │   │
│   │  └───────────────────────────────────────────────────────────┘     │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          SUPPORTING LAYERS                                   │
│                                                                              │
│   ┌───────────────┐  ┌────────────────┐  ┌──────────────┐  ┌─────────────┐  │
│   │  Resilience   │  │    Security    │  │   Logging    │  │    JSON     │  │
│   │               │  │                │  │              │  │             │  │
│   │ •CircuitBreaker│ │ •URL Validator │  │ •ILogger     │  │ •fast_json  │  │
│   │ •RetryPolicy  │  │ •SSRF protect  │  │ •Console     │  │ •simdjson   │  │
│   │ •BackoffPolicy│  │                │  │ •spdlog      │  │             │  │
│   └───────────────┘  └────────────────┘  └──────────────┘  └─────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Component Relationships

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          CLASS DEPENDENCY GRAPH                              │
└─────────────────────────────────────────────────────────────────────────────┘

                              ┌────────────────────┐
                              │  McpClientConfig   │
                              └─────────┬──────────┘
                                        │
         ┌──────────────────────────────┼──────────────────────────────┐
         │                              │                              │
         ▼                              ▼                              ▼
┌─────────────────┐          ┌─────────────────────┐        ┌──────────────────┐
│HttpTransportConf│          │CircuitBreakerConfig │        │ClientCapabilities│
└────────┬────────┘          └──────────┬──────────┘        └──────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐          ┌─────────────────────┐
│  HttpTransport  │◀─────────│     McpClient       │
│                 │          │                     │
│ •IHttpClient    │          │ •CircuitBreaker     │
│ •SessionManager │          │ •IElicitationHandler│
│ •SseParser      │          │ •ISamplingHandler   │
│ •RetryPolicy    │          │ •IRootsHandler      │
│ •BackoffPolicy  │          └─────────────────────┘
└─────────────────┘                    │
         │                             │ (parallel async version)
         │                             ▼
         │                   ┌─────────────────────┐
         │                   │   AsyncMcpClient    │
         │                   │                     │
         │                   │ •IAsyncTransport    │
         │                   │ •CircuitBreaker     │
         │                   │ •ASIO strand        │
         │                   │ •Pending requests   │
         │                   │  channel map        │
         │                   └─────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          INTERFACE HIERARCHY                                 │
└─────────────────────────────────────────────────────────────────────────────┘

   Transport Interfaces                    Handler Interfaces
   ═══════════════════                    ══════════════════

   ┌─────────────────┐                    ┌──────────────────────┐
   │  IHttpClient    │                    │ IElicitationHandler  │
   │  (abstract)     │                    │ (form + URL modes)   │
   └────────┬────────┘                    └──────────┬───────────┘
            │                                        │
            ▼                                        ├───▶ NullElicitationHandler
   ┌─────────────────┐                              │
   │  HttpClientCpr  │                    ┌─────────────────────┐
   │  (cpr impl)     │                    │  ISamplingHandler   │
   └─────────────────┘                    │  (LLM integration)  │
                                          └─────────┬───────────┘
   ┌─────────────────┐                              │
   │IAsyncTransport  │                              ├───▶ NullSamplingHandler
   │  (abstract)     │                              │
   └────────┬────────┘                    ┌─────────────────────┐
            │                             │   IRootsHandler     │
            ▼                             │  (filesystem roots) │
   ┌─────────────────┐                    └─────────┬───────────┘
   │AsyncProcess     │                              │
   │Transport        │                              ├───▶ StaticRootsHandler
   └─────────────────┘                              └───▶ MutableRootsHandler
```

---

## Data Flow Diagrams

### Request/Response Flow (Synchronous)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     SYNCHRONOUS REQUEST FLOW                                 │
└─────────────────────────────────────────────────────────────────────────────┘

  Application          McpClient           HttpTransport        MCP Server
       │                   │                    │                    │
       │  call_tool()      │                    │                    │
       │──────────────────▶│                    │                    │
       │                   │                    │                    │
       │                   │ Check circuit      │                    │
       │                   │ breaker ──────────▶│                    │
       │                   │    │               │                    │
       │                   │    │ OPEN?         │                    │
       │                   │◀───┘               │                    │
       │                   │                    │                    │
       │                   │ Build JSON-RPC     │                    │
       │                   │ request            │                    │
       │                   │                    │                    │
       │                   │ send(request)      │                    │
       │                   │───────────────────▶│                    │
       │                   │                    │                    │
       │                   │                    │  HTTP POST         │
       │                   │                    │───────────────────▶│
       │                   │                    │                    │
       │                   │                    │   SSE response     │
       │                   │                    │◀───────────────────│
       │                   │                    │                    │
       │                   │                    │ Parse SSE events   │
       │                   │                    │ Enqueue message    │
       │                   │                    │                    │
       │                   │ receive()          │                    │
       │                   │───────────────────▶│                    │
       │                   │                    │                    │
       │                   │ JSON response      │                    │
       │                   │◀───────────────────│                    │
       │                   │                    │                    │
       │                   │ Record success     │                    │
       │                   │ (circuit breaker)  │                    │
       │                   │                    │                    │
       │  CallToolResult   │                    │                    │
       │◀──────────────────│                    │                    │
       │                   │                    │                    │
```

### Async Message Dispatch Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     ASYNC MESSAGE DISPATCHER                                 │
└─────────────────────────────────────────────────────────────────────────────┘

                            AsyncMcpClient
                                  │
                                  │ spawn message_dispatcher()
                                  ▼
                    ┌─────────────────────────────┐
                    │  message_dispatcher loop    │
                    │                             │
                    │  while (connected_) {       │
                    │    message = co_await       │
                    │      transport.receive()    │
                    │                             │
                    │    // Check message type    │
                    │  }                          │
                    └──────────────┬──────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
              ▼                    ▼                    ▼
     ┌────────────────┐   ┌────────────────┐   ┌────────────────┐
     │ has id + method│   │  has id only   │   │has method only │
     │                │   │                │   │                │
     │ SERVER REQUEST │   │   RESPONSE     │   │ NOTIFICATION   │
     └───────┬────────┘   └───────┬────────┘   └───────┬────────┘
             │                    │                    │
             ▼                    ▼                    ▼
     dispatch_server_     dispatch_response()  dispatch_notification()
     request()                   │                    │
             │                   │                    │
     ┌───────┴───────┐          │            ┌───────┴───────┐
     │               │          │            │               │
     ▼               ▼          ▼            ▼               ▼
elicitation/   sampling/   Send to       tools/list    resources/
   create     createMessage pending      changed       updated
     │            │        request         │               │
     ▼            ▼        channel         ▼               ▼
handle_        handle_        │      callback         callback
elicitation    sampling       │      handlers         handlers
     │            │           │
     └────────────┴───────────┘
               │
               ▼
        send_response()
```

---

## Layer Architecture

### Client Layer

The client layer provides high-level APIs for MCP operations.

| Component | File | Description |
|-----------|------|-------------|
| `McpClient` | `client/mcp_client.hpp` | Synchronous MCP client with blocking operations |
| `AsyncMcpClient` | `async/async_mcp_client.hpp` | C++20 coroutine-based async client |
| `ClientError` | `client/client_error.hpp` | Unified error types for both clients |
| `McpClientConfig` | `client/mcp_client.hpp` | Configuration for sync client |
| `AsyncMcpClientConfig` | `async/async_mcp_client.hpp` | Configuration for async client |

**Handler Interfaces:**

| Interface | File | Purpose |
|-----------|------|---------|
| `IElicitationHandler` | `client/elicitation_handler.hpp` | Handle server requests for user input |
| `ISamplingHandler` | `client/sampling_handler.hpp` | Handle LLM completion requests |
| `IRootsHandler` | `client/roots_handler.hpp` | Provide filesystem root information |
| `IAsyncElicitationHandler` | `client/async_handlers.hpp` | Async version of elicitation handler |
| `IAsyncSamplingHandler` | `client/async_handlers.hpp` | Async version of sampling handler |
| `IAsyncRootsHandler` | `client/async_handlers.hpp` | Async version of roots handler |

### Transport Layer

The transport layer handles communication with MCP servers.

```
                          Transport Abstraction
                                   │
              ┌────────────────────┴────────────────────┐
              │                                         │
              ▼                                         ▼
       ┌──────────────┐                          ┌──────────────┐
       │ HTTP/SSE     │                          │ Stdio/Process│
       │ Transport    │                          │ Transport    │
       └──────────────┘                          └──────────────┘
              │                                         │
    ┌─────────┴─────────┐                              │
    │                   │                              │
    ▼                   ▼                              ▼
IHttpClient      SessionManager              fork() + exec()
    │                   │                    Content-Length
    ▼                   │                    framing
HttpClientCpr          ▼
(cpr library)    State Machine
                 Callbacks
```

| Component | File | Description |
|-----------|------|-------------|
| `HttpTransport` | `transport/http_transport.hpp` | HTTP transport with SSE streaming |
| `ProcessTransport` | `transport/process_transport.hpp` | Subprocess stdio transport |
| `IHttpClient` | `transport/http_client.hpp` | Abstract HTTP client interface |
| `HttpClientCpr` | `src/transport/http_client_cpr.cpp` | cpr-based HTTP implementation |
| `IAsyncTransport` | `async/async_transport.hpp` | Abstract async transport interface |
| `AsyncProcessTransport` | `src/async/async_process_transport.cpp` | ASIO-based async process transport |
| `SessionManager` | `transport/session_manager.hpp` | Session state machine and callbacks |
| `SseParser` | `transport/sse_parser.hpp` | Server-Sent Events incremental parser |

### Protocol Layer

Defines all MCP protocol types and JSON-RPC messaging.

| Component | File | Description |
|-----------|------|-------------|
| `MCP Types` | `protocol/mcp_types.hpp` | All MCP protocol structures (~1700 lines) |
| `JsonRpcRequest` | `protocol/json_rpc.hpp` | JSON-RPC 2.0 request building |
| `JsonRpcNotification` | `protocol/json_rpc.hpp` | JSON-RPC 2.0 notifications |

**Key Protocol Types:**

```
MCP Protocol Types
├── Initialization
│   ├── InitializeParams
│   ├── InitializeResult
│   ├── ClientCapabilities
│   └── ServerCapabilities
├── Tools
│   ├── Tool
│   ├── ToolAnnotations
│   ├── ListToolsResult
│   ├── CallToolParams
│   └── CallToolResult
├── Resources
│   ├── Resource
│   ├── ResourceContents
│   ├── ListResourcesResult
│   ├── ReadResourceResult
│   └── ResourceTemplate
├── Prompts
│   ├── Prompt
│   ├── PromptArgument
│   ├── PromptMessage
│   ├── ListPromptsResult
│   └── GetPromptResult
├── Completion
│   ├── CompleteParams
│   └── CompleteResult
├── Elicitation (MCP 2025)
│   ├── FormElicitationParams
│   ├── UrlElicitationParams
│   └── ElicitationResult
├── Sampling (MCP 2025)
│   ├── CreateMessageParams
│   ├── CreateMessageResult
│   └── SamplingMessage
├── Roots (MCP 2025)
│   ├── Root
│   └── ListRootsResult
├── Logging
│   ├── LoggingLevel
│   └── SetLoggingLevelParams
├── Progress
│   ├── ProgressToken
│   └── ProgressNotification
└── Errors
    ├── McpError
    └── ErrorCode
```

---

## Module Breakdown

### Client Layer

#### `McpClient` (Synchronous)

**Purpose:** High-level synchronous client for MCP server interaction.

**Key Features:**
- Blocking request/response semantics
- Automatic initialization handshake
- Circuit breaker integration for resilience
- Event callback registration
- Handler injection for server requests

**Important Methods:**

```cpp
// Connection lifecycle
McpResult<InitializeResult> connect();
void disconnect();
bool is_connected() const;
bool is_initialized() const;

// MCP Operations
McpResult<ListToolsResult> list_tools(std::optional<std::string> cursor = std::nullopt);
McpResult<CallToolResult> call_tool(const std::string& name, const Json& arguments);
McpResult<ListResourcesResult> list_resources();
McpResult<ReadResourceResult> read_resource(const std::string& uri);
McpResult<ListPromptsResult> list_prompts();
McpResult<GetPromptResult> get_prompt(const std::string& name, const std::unordered_map<std::string, std::string>& args);

// Circuit breaker control
CircuitState circuit_state() const;
void force_circuit_open();
void force_circuit_closed();
```

**Internal Flow:**

```
┌──────────────────────────────────────────────────────────────────┐
│                    send_and_receive() Method                      │
└──────────────────────────────────────────────────────────────────┘

  1. Check circuit breaker ──▶ If OPEN, fail fast
         │
         ▼
  2. transport_->send(request)
         │
         ▼
  3. Loop: transport_->receive()
         │
         ├──▶ Server Request? ──▶ handle_server_request()
         │                              │
         │                              ▼
         │                        send_response()
         │
         ├──▶ Notification? ──▶ dispatch_notification()
         │
         └──▶ Response? ──▶ extract_result()
                                │
                                ▼
  4. Record success/failure in circuit breaker
         │
         ▼
  5. Return result
```

#### `AsyncMcpClient` (Coroutine-based)

**Purpose:** Non-blocking MCP client using C++20 coroutines with ASIO.

**Key Differences from Sync Client:**
- All operations return `asio::awaitable<AsyncMcpResult<T>>`
- Uses ASIO strand for thread safety
- Dedicated message dispatcher coroutine
- Pending requests stored in channel-based map
- Supports both sync and async handlers

**Request/Response Correlation:**

```cpp
struct PendingRequest {
    using ResponseChannel = asio::experimental::channel<
        void(asio::error_code, AsyncMcpResult<Json>)
    >;
    std::shared_ptr<ResponseChannel> channel;  // Response delivery
    std::unique_ptr<asio::steady_timer> timeout_timer;
};

std::unordered_map<int, std::unique_ptr<PendingRequest>> pending_requests_;
```

**Message Dispatcher Logic:**

```cpp
asio::awaitable<void> message_dispatcher() {
    while (connected_) {
        auto result = co_await transport_->async_receive();
        if (!result) break;
        
        const Json& message = *result;
        bool has_id = message.contains("id") && !message["id"].is_null();
        bool has_method = message.contains("method");
        
        if (has_method && has_id) {
            // Server request - handle and respond
            co_await dispatch_server_request(message);
        }
        else if (has_id) {
            // Response - send to waiting coroutine via channel
            dispatch_response(id, message);
        }
        else if (has_method) {
            // Notification - dispatch to callbacks
            dispatch_notification(method, params);
        }
    }
}
```

### Transport Layer

#### `HttpTransport`

**Purpose:** HTTP-based transport implementing MCP's "Streamable HTTP" protocol.

**Protocol Details:**
- **POST** requests for sending JSON-RPC messages
- **SSE** (Server-Sent Events) for receiving server messages
- **DELETE** for session termination
- Session ID management via `Mcp-Session-Id` header

**Threading Model:**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         HttpTransport Threads                                │
└─────────────────────────────────────────────────────────────────────────────┘

    Main Thread                          SSE Reader Thread
         │                                      │
         │ send(message)                        │ sse_reader_loop()
         │      │                               │      │
         ▼      ▼                               ▼      ▼
    do_post_with_retry()                  GET request (blocking)
         │                                      │
         ▼                                      ▼
    POST to server                        Process SSE events
         │                                      │
         ▼                                      ▼
    Process SSE response               enqueue_message()
    if Content-Type:                         │
    text/event-stream                        ▼
         │                              Queue + CV notify
         ▼                                      │
    enqueue_message() ◀─────────────────────────┘
         │
         ▼
    message_queue_ (thread-safe)
         ▲
         │
    receive() blocks on queue_cv_
```

**Session State Machine:**

```
         ┌──────────────┐
         │ Disconnected │◀──────────────────────┐
         └──────┬───────┘                       │
                │ start()                       │
                ▼                               │
         ┌──────────────┐                       │
    ┌────│  Connecting  │────┐                  │
    │    └──────────────┘    │                  │
    │                        │                  │
    │ success                │ failure         │
    ▼                        ▼                  │
┌──────────┐           ┌──────────┐             │
│Connected │           │  Failed  │─────────────┤
└────┬─────┘           └──────────┘             │
     │ 404                  ▲                   │
     ▼                      │                   │
┌────────────┐              │                   │
│Reconnecting│──────────────┘ failure           │
└────┬───────┘                                  │
     │ success                                  │
     └─────────▶ Connected                      │
                                                │
┌──────────────┐                                │
│   Closing    │────────────────────────────────┘
└──────────────┘ close_complete()
```

#### `ProcessTransport`

**Purpose:** Spawn and communicate with MCP servers via stdin/stdout pipes.

**Process Lifecycle:**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       ProcessTransport Lifecycle                             │
└─────────────────────────────────────────────────────────────────────────────┘

  start()
     │
     ├──▶ Validate command (security check)
     │         │
     │         ├── Reject shell metacharacters
     │         └── Allow only standard bin paths
     │
     ├──▶ Create pipes (stdin_pipe, stdout_pipe)
     │
     ├──▶ fork()
     │         │
     │         ├── Child: dup2() stdin/stdout, execvp()
     │         │
     │         └── Parent: close unused pipe ends
     │
     └──▶ Store PID, FDs, set running_ = true

  send(Json)
     │
     ├──▶ Check running_, process alive
     │
     ├──▶ Serialize JSON
     │
     ├──▶ Frame with Content-Length (if enabled)
     │         "Content-Length: 42\r\n\r\n{...json...}"
     │
     └──▶ write() to stdin_fd_

  receive()
     │
     ├──▶ Check running_, process alive
     │
     ├──▶ poll() for readability (with timeout)
     │
     └──▶ receive_framed() or receive_line()
                │
                ├── Read headers until \r\n\r\n
                ├── Parse Content-Length
                ├── read_exact(content_length bytes)
                └── JSON::parse()

  stop()
     │
     ├──▶ close(stdin_fd_), close(stdout_fd_)
     │
     ├──▶ kill(SIGTERM), wait 100ms
     │
     └──▶ kill(SIGKILL) if still running, waitpid()
```

**Buffered I/O:**

```cpp
// Read buffer to avoid syscall-per-byte
static constexpr std::size_t read_buffer_size = 8192;
char read_buffer_[read_buffer_size];
std::size_t read_buffer_pos_{0};
std::size_t read_buffer_len_{0};

TransportResult<std::size_t> fill_read_buffer();
TransportResult<void> read_exact(char* dest, std::size_t n);
```

#### `SseParser`

**Purpose:** Incremental parser for Server-Sent Events streams.

**SSE Format:**
```
event: message
id: 12345
data: {"jsonrpc": "2.0", "result": {...}}

retry: 5000
data: next event data
```

**Parsing Algorithm:**

```
feed(chunk) ──▶ Append to buffer_
                    │
                    ▼
            Loop: Find complete lines
                    │
                    ├──▶ "event: X" ──▶ current_event_ = X
                    │
                    ├──▶ "id: X" ──▶ current_id_ = X
                    │
                    ├──▶ "data: X" ──▶ current_data_ += X + "\n"
                    │
                    ├──▶ "retry: X" ──▶ current_retry_ = X
                    │
                    └──▶ Blank line ──▶ emit_event()
                                            │
                                            ▼
                                       Return SseEvent
```

### Resilience Layer

#### `CircuitBreaker`

**Purpose:** Prevent cascading failures by failing fast when service is unhealthy.

**State Machine:**

```
         ┌─────────┐  failure_threshold   ┌────────┐
         │ CLOSED  │ ─────────────────────▶│  OPEN  │
         └────┬────┘   consecutive         └────┬───┘
              │        failures                 │
              │                                 │ recovery_timeout
              │                                 ▼
              │                           ┌──────────┐
              │                           │HALF_OPEN │
              │                           └────┬─────┘
              │                                │
              │◀───────────────────────────────┤ success
              │         reset to closed        │
              │                                │ failure
              │                                ▼
              │                           ┌────────┐
              └───────────────────────────│  OPEN  │
                                          └────────┘
```

**Configuration:**

```cpp
struct CircuitBreakerConfig {
    std::size_t failure_threshold{5};     // Failures before open
    std::chrono::milliseconds recovery_timeout{30000};  // Time in open state
    std::size_t success_threshold{1};     // Successes in half-open to close
    std::string name{"default"};          // For logging/metrics
};
```

**RAII Guard:**

```cpp
class CircuitBreakerGuard {
public:
    explicit CircuitBreakerGuard(CircuitBreaker& breaker);
    ~CircuitBreakerGuard();  // Records success or failure
    void mark_success() noexcept;
private:
    CircuitBreaker& breaker_;
    bool success_;
};
```

#### `RetryPolicy` and `BackoffPolicy`

**RetryPolicy** - Defines *which* errors trigger retries:

```cpp
class RetryPolicy {
    std::size_t max_attempts_{3};
    bool retry_on_connection_error_{true};
    bool retry_on_timeout_{true};
    bool retry_on_ssl_error_{false};
    std::set<int> retryable_http_statuses_{429, 500, 502, 503, 504};
    
    bool should_retry(HttpTransportError::Code code, std::size_t attempt);
    bool should_retry_http_status(int status_code);
};
```

**BackoffPolicy** - Defines *how long* to wait between retries:

```cpp
struct IBackoffPolicy {
    virtual std::chrono::milliseconds next_delay(std::size_t attempt) = 0;
    virtual void reset() = 0;
};

class ExponentialBackoff : public IBackoffPolicy {
    // delay = min(base * (multiplier ^ attempt) + jitter, max)
    // Default: 100ms base, 2x multiplier, 30s max, ±25% jitter
};

class NoBackoff : public IBackoffPolicy {
    // Returns 0ms - for testing
};

class ConstantBackoff : public IBackoffPolicy {
    // Returns fixed delay
};
```

### Security Layer

#### `UrlValidator`

**Purpose:** Validate URLs for elicitation URL mode to prevent SSRF and other attacks.

**Security Checks:**

```
validate_url(url, config)
         │
         ├──▶ Parse with ada-url (WHATWG compliant)
         │
         ├──▶ Check scheme (HTTPS required by default)
         │
         ├──▶ Check for embedded credentials (user:pass@host)
         │         └── Always blocked
         │
         ├──▶ Check localhost/loopback
         │         └── Blocked unless allow_localhost
         │
         ├──▶ Check private IP ranges (RFC 1918)
         │         ├── 10.0.0.0/8
         │         ├── 172.16.0.0/12
         │         └── 192.168.0.0/16
         │
         ├──▶ Check link-local (169.254.x.x, fe80::/10)
         │
         ├──▶ Check whitelist/blacklist
         │
         ├──▶ Check URL length (max 2048)
         │
         └──▶ Check subdomain depth (max 5)
```

**Result Structure:**

```cpp
struct UrlValidationResult {
    bool is_valid;           // URL is well-formed
    bool is_safe;            // Passes security checks
    std::string display_domain;   // For user consent UI
    std::string normalized_url;   // For opening
    std::optional<std::string> warning;  // Non-blocking concern
    std::optional<std::string> error;    // Blocking issue
};
```

### Logging Layer

**ILogger Interface:**

```cpp
class ILogger {
public:
    virtual void log(const LogRecord& record) = 0;
    virtual bool should_log(LogLevel level) const noexcept = 0;
    
    // Convenience methods
    void trace/debug/info/warn/error/fatal(std::string_view msg);
    
    // Templated formatting (C++20 std::format)
    template<typename... Args>
    void trace_fmt/debug_fmt/info_fmt/...(std::format_string<Args...> fmt, Args&&... args);
};
```

**Implementations:**

| Logger | Purpose |
|--------|---------|
| `NullLogger` | Discards all logs (zero overhead) |
| `ConsoleLogger` | Outputs to stderr with ANSI colors |
| `SpdlogLogger` | Integration with spdlog library |

**Global Logger:**

```cpp
ILogger& get_logger() noexcept;
void set_logger(std::unique_ptr<ILogger> logger) noexcept;

// Macros for zero-overhead when disabled
MCPP_LOG_TRACE(msg)
MCPP_LOG_DEBUG(msg)
MCPP_LOG_INFO(msg)
MCPP_LOG_WARN(msg)
MCPP_LOG_ERROR(msg)
MCPP_LOG_FATAL(msg)
```

---

## File-by-File Documentation

### Headers (`include/mcpp/`)

#### `transport.hpp`
- **Purpose:** Common transport types and error definitions
- **Key Types:** `TransportError`, `TransportResult<T>`, `HeaderMap`
- **Lines:** ~37

#### `protocol.hpp`
- **Purpose:** Aggregates protocol headers
- **Includes:** `mcpp/protocol/json_rpc.hpp`
- **Lines:** ~4

#### `protocol/json_rpc.hpp`
- **Purpose:** JSON-RPC 2.0 message building
- **Key Types:** `JsonRpcId`, `JsonRpcRequest`, `JsonRpcNotification`, `JsonRpcError`
- **Lines:** ~83

#### `protocol/mcp_types.hpp`
- **Purpose:** Complete MCP protocol type definitions
- **Key Sections:**
  - Initialization (params, result, capabilities)
  - Tools (Tool, ToolAnnotations, ListToolsResult, CallToolResult)
  - Resources (Resource, ResourceContents, subscriptions, templates)
  - Prompts (Prompt, PromptArgument, GetPromptResult)
  - Completion (CompleteParams, CompleteResult)
  - Elicitation (Form/URL modes, ElicitationResult)
  - Sampling (CreateMessageParams/Result, ModelPreferences)
  - Roots (Root, ListRootsResult)
  - Logging (LoggingLevel)
  - Progress (ProgressToken, ProgressNotification)
  - Errors (McpError, ErrorCode namespace)
- **Lines:** ~1680

#### `client/mcp_client.hpp`
- **Purpose:** Synchronous MCP client
- **Key Methods:** `connect()`, `list_tools()`, `call_tool()`, `list_resources()`, `get_prompt()`
- **Features:** Circuit breaker, handler registration, event callbacks
- **Lines:** ~339

#### `client/client_error.hpp`
- **Purpose:** Unified error types for sync/async clients
- **Key Types:** `ClientError`, `ClientErrorCode`, `ClientResult<T>`
- **Aliases:** `McpClientError`, `McpResult<T>`, `AsyncMcpClientError`, `AsyncMcpResult<T>`
- **Lines:** ~110

#### `client/elicitation_handler.hpp`
- **Purpose:** Interface for handling server elicitation requests
- **Key Interface:** `IElicitationHandler`
- **Methods:** `handle_form()`, `handle_url()`
- **Implementations:** `NullElicitationHandler`
- **Lines:** ~124

#### `client/sampling_handler.hpp`
- **Purpose:** Interface for handling LLM completion requests
- **Key Interface:** `ISamplingHandler`
- **Methods:** `handle_create_message()`
- **Implementations:** `NullSamplingHandler`
- **Lines:** ~107

#### `client/roots_handler.hpp`
- **Purpose:** Interface for filesystem root management
- **Key Interface:** `IRootsHandler`
- **Methods:** `list_roots()`
- **Implementations:** `StaticRootsHandler`, `MutableRootsHandler`
- **Lines:** ~128

#### `client/async_handlers.hpp`
- **Purpose:** Async (coroutine) versions of handler interfaces
- **Key Interfaces:** `IAsyncElicitationHandler`, `IAsyncSamplingHandler`, `IAsyncRootsHandler`
- **Implementations:** Null async handlers
- **Lines:** ~118

#### `async/async_mcp_client.hpp`
- **Purpose:** C++20 coroutine-based async MCP client
- **Key Features:**
  - All methods return `asio::awaitable<AsyncMcpResult<T>>`
  - ASIO strand for thread safety
  - Message dispatcher coroutine
  - Pending request channel map
  - Both sync and async handler support
- **Lines:** ~406

#### `async/async_transport.hpp`
- **Purpose:** Abstract interface for async transports
- **Key Interface:** `IAsyncTransport`
- **Methods:** `async_start()`, `async_stop()`, `async_send()`, `async_receive()`
- **Lines:** ~83

#### `transport/http_transport.hpp`
- **Purpose:** HTTP transport with SSE streaming
- **Key Features:**
  - POST for sending, GET+SSE for receiving
  - Session management
  - Thread-safe message queue
  - Retry with backoff
- **Lines:** ~191

#### `transport/http_client.hpp`
- **Purpose:** Abstract HTTP client interface
- **Key Interface:** `IHttpClient`
- **Methods:** `get()`, `post()`, `del()`, `async_get()`, `async_post()`, `async_del()`
- **Lines:** ~183

#### `transport/http_transport_config.hpp`
- **Purpose:** Configuration for HTTP transport
- **Key Configs:**
  - `TlsConfig` (CA cert, mTLS, verification)
  - `HttpTransportConfig` (URLs, timeouts, retries, SSE settings)
- **Lines:** ~152

#### `transport/http_types.hpp`
- **Purpose:** HTTP types and URL parsing
- **Key Types:** `HttpMethod`, `HttpRequest`, `HttpResponse`, `UrlComponents`
- **Functions:** `parse_url()`, `get_header()`, `find_header()`
- **Lines:** ~207

#### `transport/transport_error.hpp`
- **Purpose:** HTTP transport error types
- **Key Types:** `HttpTransportError`, `HttpResult<T>`
- **Error Codes:** ConnectionFailed, Timeout, SslError, SessionExpired, etc.
- **Lines:** ~85

#### `transport/session_manager.hpp`
- **Purpose:** Session state machine and lifecycle callbacks
- **Key Types:** `SessionState`, `SessionManager`
- **Callbacks:** `StateChangeCallback`, `SessionEstablishedCallback`, `SessionLostCallback`
- **Lines:** ~279

#### `transport/sse_parser.hpp`
- **Purpose:** Incremental SSE stream parser
- **Key Types:** `SseEvent`, `SseParser`
- **Methods:** `feed()`, `reset()`
- **Lines:** ~72

#### `transport/process_transport.hpp`
- **Purpose:** Subprocess stdio transport
- **Key Types:** `ProcessTransportConfig`, `ProcessTransport`, `StderrHandling`
- **Features:** fork/exec, Content-Length framing, buffered I/O
- **Lines:** ~124

#### `transport/retry_policy.hpp`
- **Purpose:** Define which errors trigger retries
- **Key Type:** `RetryPolicy`
- **Lines:** ~146

#### `transport/backoff_policy.hpp`
- **Purpose:** Calculate retry delays
- **Key Types:** `IBackoffPolicy`, `ExponentialBackoff`, `NoBackoff`, `ConstantBackoff`
- **Lines:** ~170

#### `resilience/circuit_breaker.hpp`
- **Purpose:** Circuit breaker pattern implementation
- **Key Types:** `CircuitState`, `CircuitBreakerConfig`, `CircuitBreaker`, `CircuitBreakerGuard`
- **Lines:** ~245

#### `security/url_validator.hpp`
- **Purpose:** URL validation for security
- **Key Types:** `UrlValidationConfig`, `UrlValidationResult`
- **Functions:** `validate_url()`, various `is_*()` helpers
- **Lines:** ~122

#### `log/logger.hpp`
- **Purpose:** Logging abstraction
- **Key Types:** `LogLevel`, `LogRecord`, `ILogger`, `NullLogger`, `ConsoleLogger`
- **Macros:** `MCPP_LOG_*`
- **Lines:** ~245

#### `json/fast_json.hpp`
- **Purpose:** Fast JSON parsing with simdjson
- **Functions:** `fast_parse()`, `fast_parse_many()`
- **Lines:** ~small

### Source Files (`src/`)

#### `client/mcp_client.cpp`
- **Purpose:** McpClient implementation
- **Key Functions:**
  - `connect()` - Initialize transport and MCP handshake
  - `send_and_receive()` - Core request/response loop with circuit breaker
  - `handle_server_request()` - Route elicitation/sampling/roots requests
  - `dispatch_notification()` - Route notifications to callbacks (O(1) dispatch table)
- **Lines:** ~833

#### `async/async_mcp_client.cpp`
- **Purpose:** AsyncMcpClient implementation
- **Key Functions:**
  - `connect()` - Start transport, spawn dispatcher, initialize
  - `send_request()` - Create channel, send, await response with timeout
  - `message_dispatcher()` - Main receive loop coroutine
  - `dispatch_server_request()` - Handle and respond to server requests
- **Lines:** ~1049

#### `async/async_process_transport.cpp`
- **Purpose:** ASIO-based async process transport
- **Key Features:**
  - `asio::posix::stream_descriptor` for async pipe I/O
  - Message queue channel for received messages
  - Coroutine-based reader task
- **Lines:** ~moderate

#### `transport/http_transport.cpp`
- **Purpose:** HttpTransport implementation
- **Key Functions:**
  - `start()/stop()` - Lifecycle with SSE thread
  - `do_post_with_retry()` - Retry loop with backoff
  - `process_sse_response()` - Parse SSE events
  - `sse_reader_loop()` - Background SSE connection
- **Lines:** ~686

#### `transport/process_transport.cpp`
- **Purpose:** ProcessTransport implementation
- **Key Functions:**
  - `start()` - Validate command, create pipes, fork, exec
  - `send()` - Frame and write to stdin
  - `receive_framed()` - Parse Content-Length header, read body
  - `fill_read_buffer()`, `read_exact()` - Buffered I/O
- **Lines:** ~577

#### `transport/http_client_cpr.cpp`
- **Purpose:** cpr library HTTP client implementation
- **Implements:** `IHttpClient` interface
- **Lines:** ~moderate

#### `transport/session_manager.cpp`
- **Purpose:** SessionManager state transitions and callbacks
- **Lines:** ~moderate

#### `transport/sse_parser.cpp`
- **Purpose:** SSE incremental parsing
- **Lines:** ~moderate

#### `transport/http_types.cpp`
- **Purpose:** URL parsing using ada-url library
- **Lines:** ~small

#### `resilience/circuit_breaker.cpp`
- **Purpose:** CircuitBreaker state machine logic
- **Lines:** ~moderate

#### `security/url_validator.cpp`
- **Purpose:** URL security validation implementation
- **Lines:** ~moderate

#### `log/logger.cpp`
- **Purpose:** Global logger management, ConsoleLogger output
- **Lines:** ~moderate

#### `json/fast_json.cpp`
- **Purpose:** simdjson integration
- **Lines:** ~small

### CLI Tool (`tools/mcpp-cli/`)

#### `main.cpp`
- **Purpose:** Command-line MCP server testing tool
- **Features:**
  - List tools/resources/prompts/templates
  - Call tools with JSON arguments
  - Read resources
  - Interactive REPL mode
  - JSON output for scripting
  - ANSI color output
- **Lines:** ~710

---

## Key Concepts

### Error Handling with `tl::expected`

The codebase uses `tl::expected<T, E>` instead of exceptions for error handling:

```cpp
// Return type signals potential failure
McpResult<ListToolsResult> list_tools();

// Usage
auto result = client.list_tools();
if (!result) {
    // Handle error
    std::cerr << result.error().message;
} else {
    // Use value
    for (const auto& tool : result->tools) { ... }
}
```

**Benefits:**
- Explicit error paths
- No hidden control flow
- Zero-cost when successful
- Composable with monadic operations

### Thread Safety Patterns

**Atomic State Flags:**
```cpp
std::atomic<bool> connected_{false};
std::atomic<bool> initialized_{false};
std::atomic<uint64_t> request_id_{0};
```

**Mutex-Protected Data:**
```cpp
mutable std::mutex mutex_;
std::queue<Json> message_queue_;
std::condition_variable queue_cv_;
```

**ASIO Strand (async client):**
```cpp
asio::strand<asio::any_io_executor> strand_;

// Ensure operations run on strand
co_await asio::dispatch(asio::bind_executor(strand_, asio::use_awaitable));
```

### JSON-RPC Message Types

```cpp
// Determined by presence of "id" and "method" fields:

// Request (has both id and method)
{"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {...}}

// Response (has id, no method)
{"jsonrpc": "2.0", "id": 1, "result": {...}}
// or
{"jsonrpc": "2.0", "id": 1, "error": {"code": -32600, "message": "..."}}

// Notification (has method, no id)
{"jsonrpc": "2.0", "method": "notifications/initialized", "params": {...}}
```

---

## Design Patterns Used

| Pattern | Where Used | Purpose |
|---------|-----------|---------|
| **Strategy** | `IBackoffPolicy`, `IHttpClient` | Swappable algorithms |
| **Factory** | `make_http_client()`, `make_async_process_transport()` | Decouple creation |
| **State Machine** | `SessionManager`, `CircuitBreaker` | Explicit state transitions |
| **Observer** | Notification handlers | Event subscription |
| **RAII** | `CircuitBreakerGuard` | Resource management |
| **Null Object** | `NullLogger`, `NullElicitationHandler` | Default implementations |
| **Builder** | `HttpTransportConfig`, `RetryPolicy` | Fluent configuration |
| **Dependency Injection** | Handler interfaces | Testability |
| **Decorator** | Future: logging/metrics HTTP client wrapper | Cross-cutting concerns |

---

## External Dependencies

| Library | Purpose | Used In |
|---------|---------|---------|
| `nlohmann/json` | JSON parsing/serialization | Throughout |
| `simdjson` | SIMD-accelerated JSON parsing | `fast_json.cpp` |
| `tl::expected` | Error handling | All result types |
| `cpr` | HTTP client | `http_client_cpr.cpp` |
| `asio` | Async I/O, coroutines | Async client/transport |
| `ada-url` | URL parsing (WHATWG) | `http_types.cpp`, `url_validator.cpp` |
| `spdlog` | Logging | `spdlog_logger.cpp` |
| `cxxopts` | CLI parsing | `mcpp-cli` |
| `Catch2` | Testing | All tests |

---

*Generated from MCPP codebase analysis*

