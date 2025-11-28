// ─────────────────────────────────────────────────────────────────────────────
// Real Server Tests
// ─────────────────────────────────────────────────────────────────────────────
// Integration tests against real MCP servers via stdio and HTTP transports.
//
// These tests require external MCP servers to be available.
// Run with: ./mcpp_tests "[real]"
//
// Prerequisites:
//   - Node.js/npm installed
//   - npx available in PATH
//   - For HTTP tests: GITHUB_MCP_PAT environment variable set

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/transport/process_transport.hpp"
#include "mcpp/transport/http_transport.hpp"
#include "mcpp/protocol/mcp_types.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace mcpp;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Test Helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

bool is_npx_available() {
    return std::system("which npx > /dev/null 2>&1") == 0;
}

std::string get_test_directory() {
    // Use /tmp for testing - resolve symlinks for macOS compatibility
    auto path = std::filesystem::temp_directory_path() / "mcpp_test";
    std::filesystem::create_directories(path);
    // Resolve symlinks (important on macOS where /tmp -> /private/var/...)
    return std::filesystem::canonical(path).string();
}

// Simple MCP client using ProcessTransport
class SimpleMcpClient {
public:
    explicit SimpleMcpClient(ProcessTransport& transport)
        : transport_(transport)
    {}

    TransportResult<Json> request(const std::string& method, const Json& params = {}) {
        Json req = {
            {"jsonrpc", "2.0"},
            {"id", ++request_id_},
            {"method", method}
        };
        if (params.empty() == false) {
            req["params"] = params;
        }

        auto send_result = transport_.send(req);
        if (send_result.has_value() == false) {
            return tl::unexpected(send_result.error());
        }

        return transport_.receive();
    }

    TransportResult<void> notify(const std::string& method, const Json& params = {}) {
        Json notification = {
            {"jsonrpc", "2.0"},
            {"method", method}
        };
        if (params.empty() == false) {
            notification["params"] = params;
        }

        return transport_.send(notification);
    }

    TransportResult<InitializeResult> initialize(const std::string& client_name = "mcpp-test") {
        InitializeParams params;
        params.client_info = {client_name, "1.0.0"};

        auto result = request("initialize", params.to_json());
        if (result.has_value() == false) {
            return tl::unexpected(result.error());
        }

        if (result->contains("error")) {
            return tl::unexpected(TransportError{
                TransportError::Category::Protocol,
                (*result)["error"]["message"].get<std::string>(),
                std::nullopt
            });
        }

        // Send initialized notification
        auto notify_result = notify("notifications/initialized");
        if (notify_result.has_value() == false) {
            return tl::unexpected(notify_result.error());
        }

        return InitializeResult::from_json((*result)["result"]);
    }

private:
    ProcessTransport& transport_;
    int request_id_ = 0;
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Echo Server Tests (using npx @anthropics/echo-server if available)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport can spawn and communicate with subprocess", "[real][process]") {
    // Simple test with 'cat' command
    ProcessTransportConfig config;
    config.command = "cat";

    ProcessTransport transport(config);

    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    REQUIRE(transport.is_running());

    // Send a simple JSON message
    Json message = {{"test", "hello"}};
    auto send_result = transport.send(message);
    REQUIRE(send_result.has_value());

    // Receive the echo
    auto recv_result = transport.receive();
    REQUIRE(recv_result.has_value());
    REQUIRE((*recv_result)["test"] == "hello");

    transport.stop();
    REQUIRE(transport.is_running() == false);
}

TEST_CASE("ProcessTransport handles process not found", "[real][process]") {
    ProcessTransportConfig config;
    config.command = "nonexistent_command_12345";

    ProcessTransport transport(config);

    auto start_result = transport.start();
    // Start succeeds (fork works), but the child exits immediately
    REQUIRE(start_result.has_value());

    // Trying to receive should fail
    auto recv_result = transport.receive();
    REQUIRE(recv_result.has_value() == false);

    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// MCP Filesystem Server Tests
// ═══════════════════════════════════════════════════════════════════════════
// These tests use @modelcontextprotocol/server-filesystem

TEST_CASE("MCP filesystem server - initialize", "[real][mcp][.integration]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping MCP server tests");
    }

    std::string test_dir = get_test_directory();

    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;  // MCP filesystem server uses raw JSON

    ProcessTransport transport(config);

    auto start_result = transport.start();
    if (start_result.has_value() == false) {
        SKIP("Failed to start MCP server: " + start_result.error().message);
    }

    SimpleMcpClient client(transport);

    auto init_result = client.initialize("mcpp-real-test");
    REQUIRE(init_result.has_value());

    INFO("Server: " << init_result->server_info.name);
    INFO("Version: " << init_result->server_info.version);
    INFO("Protocol: " << init_result->protocol_version);

    REQUIRE(init_result->server_info.name.empty() == false);
    REQUIRE(init_result->protocol_version.empty() == false);

    // Check capabilities - filesystem server has tools
    REQUIRE(init_result->capabilities.tools.has_value());
    // Note: resources capability may not be present in all servers

    transport.stop();
}

TEST_CASE("MCP filesystem server - list tools", "[real][mcp][.integration]") {
    if (is_npx_available() == false) {
        SKIP("npx not available");
    }

    std::string test_dir = get_test_directory();

    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;

    ProcessTransport transport(config);

    auto start_result = transport.start();
    if (start_result.has_value() == false) {
        SKIP("Failed to start MCP server");
    }

    SimpleMcpClient client(transport);
    client.initialize();

    auto result = client.request("tools/list");
    REQUIRE(result.has_value());
    REQUIRE(result->contains("result"));

    auto tools_result = ListToolsResult::from_json((*result)["result"]);

    INFO("Found " << tools_result.tools.size() << " tools");
    for (const auto& tool : tools_result.tools) {
        INFO("  - " << tool.name);
    }

    // Filesystem server should have tools like read_file, write_file, etc.
    REQUIRE(tools_result.tools.empty() == false);

    // Check for expected tools
    bool has_read_file = false;
    bool has_write_file = false;
    for (const auto& tool : tools_result.tools) {
        if (tool.name == "read_file") has_read_file = true;
        if (tool.name == "write_file") has_write_file = true;
    }
    REQUIRE(has_read_file);
    REQUIRE(has_write_file);

    transport.stop();
}

TEST_CASE("MCP filesystem server - list resources", "[real][mcp][.integration]") {
    if (is_npx_available() == false) {
        SKIP("npx not available");
    }

    std::string test_dir = get_test_directory();

    // Create a test file
    std::filesystem::path test_file = std::filesystem::path(test_dir) / "test.txt";
    {
        std::ofstream f(test_file);
        f << "Hello from mcpp test!";
    }

    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;

    ProcessTransport transport(config);

    auto start_result = transport.start();
    if (start_result.has_value() == false) {
        SKIP("Failed to start MCP server");
    }

    SimpleMcpClient client(transport);
    auto init_result = client.initialize();
    REQUIRE(init_result.has_value());

    // Check if server supports resources
    if (!init_result->capabilities.resources.has_value()) {
        INFO("Server does not support resources capability");
        std::filesystem::remove(test_file);
        transport.stop();
        SKIP("Server does not support resources");
    }

    auto result = client.request("resources/list");
    REQUIRE(result.has_value());
    
    if (result->contains("error")) {
        INFO("Server returned error: " << (*result)["error"].dump());
        std::filesystem::remove(test_file);
        transport.stop();
        SKIP("Server returned error for resources/list");
    }
    
    REQUIRE(result->contains("result"));

    auto resources_result = ListResourcesResult::from_json((*result)["result"]);

    INFO("Found " << resources_result.resources.size() << " resources");
    for (const auto& resource : resources_result.resources) {
        INFO("  - " << resource.uri << " (" << resource.name << ")");
    }

    // Should have at least our test file
    REQUIRE(resources_result.resources.empty() == false);

    // Clean up
    std::filesystem::remove(test_file);
    transport.stop();
}

TEST_CASE("MCP filesystem server - call tool read_file", "[real][mcp][.integration]") {
    if (is_npx_available() == false) {
        SKIP("npx not available");
    }

    std::string test_dir = get_test_directory();

    // Create a test file
    std::filesystem::path test_file = std::filesystem::path(test_dir) / "read_test.txt";
    const std::string test_content = "Hello from mcpp integration test!";
    {
        std::ofstream f(test_file);
        f << test_content;
    }

    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;

    ProcessTransport transport(config);

    auto start_result = transport.start();
    if (start_result.has_value() == false) {
        SKIP("Failed to start MCP server");
    }

    SimpleMcpClient client(transport);
    client.initialize();

    // Call read_file tool
    Json params = {
        {"name", "read_file"},
        {"arguments", {{"path", test_file.string()}}}
    };

    auto result = client.request("tools/call", params);
    REQUIRE(result.has_value());

    if (result->contains("error")) {
        INFO("Error: " << (*result)["error"].dump(2));
        REQUIRE(result->contains("error") == false);
    }

    REQUIRE(result->contains("result"));
    
    INFO("Result: " << (*result)["result"].dump(2));
    
    auto call_result = CallToolResult::from_json((*result)["result"]);

    if (call_result.is_error && call_result.content.empty() == false) {
        if (std::holds_alternative<TextContent>(call_result.content[0])) {
            INFO("Error message: " << std::get<TextContent>(call_result.content[0]).text);
        }
    }

    REQUIRE(call_result.is_error == false);
    REQUIRE(call_result.content.empty() == false);

    // First content should be text with our file content
    REQUIRE(std::holds_alternative<TextContent>(call_result.content[0]));
    auto text = std::get<TextContent>(call_result.content[0]);
    REQUIRE(text.text.find(test_content) != std::string::npos);

    // Clean up
    std::filesystem::remove(test_file);
    transport.stop();
}

TEST_CASE("MCP filesystem server - call tool write_file", "[real][mcp][.integration]") {
    if (is_npx_available() == false) {
        SKIP("npx not available");
    }

    std::string test_dir = get_test_directory();
    std::filesystem::path test_file = std::filesystem::path(test_dir) / "write_test.txt";

    // Ensure file doesn't exist
    std::filesystem::remove(test_file);

    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;

    ProcessTransport transport(config);

    auto start_result = transport.start();
    if (start_result.has_value() == false) {
        SKIP("Failed to start MCP server");
    }

    SimpleMcpClient client(transport);
    client.initialize();

    // Call write_file tool
    const std::string content_to_write = "Written by mcpp test!";
    Json params = {
        {"name", "write_file"},
        {"arguments", {
            {"path", test_file.string()},
            {"content", content_to_write}
        }}
    };

    auto result = client.request("tools/call", params);
    REQUIRE(result.has_value());

    if (result->contains("error")) {
        INFO("Error: " << (*result)["error"].dump(2));
    }

    REQUIRE(result->contains("result"));
    auto call_result = CallToolResult::from_json((*result)["result"]);
    REQUIRE(call_result.is_error == false);

    // Verify file was written
    REQUIRE(std::filesystem::exists(test_file));

    std::ifstream f(test_file);
    std::string file_content((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    REQUIRE(file_content == content_to_write);

    // Clean up
    std::filesystem::remove(test_file);
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Full End-to-End Test
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("MCP filesystem server - full workflow", "[real][mcp][.integration]") {
    if (is_npx_available() == false) {
        SKIP("npx not available");
    }

    std::string test_dir = get_test_directory();

    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;

    ProcessTransport transport(config);

    auto start_result = transport.start();
    if (start_result.has_value() == false) {
        SKIP("Failed to start MCP server");
    }

    SimpleMcpClient client(transport);

    // 1. Initialize
    auto init_result = client.initialize("mcpp-e2e-test");
    REQUIRE(init_result.has_value());
    INFO("Connected to: " << init_result->server_info.name);

    // 2. List tools
    auto tools_response = client.request("tools/list");
    REQUIRE(tools_response.has_value());
    auto tools = ListToolsResult::from_json((*tools_response)["result"]);
    INFO("Available tools: " << tools.tools.size());

    // 3. Write a file
    std::filesystem::path workflow_file = std::filesystem::path(test_dir) / "workflow.txt";
    {
        Json write_params = {
            {"name", "write_file"},
            {"arguments", {
                {"path", workflow_file.string()},
                {"content", "MCP workflow test content"}
            }}
        };
        auto write_result = client.request("tools/call", write_params);
        REQUIRE(write_result.has_value());
        REQUIRE(write_result->contains("result"));
    }

    // 4. Read the file back
    {
        Json read_params = {
            {"name", "read_file"},
            {"arguments", {{"path", workflow_file.string()}}}
        };
        auto read_result = client.request("tools/call", read_params);
        REQUIRE(read_result.has_value());
        REQUIRE(read_result->contains("result"));

        auto call_result = CallToolResult::from_json((*read_result)["result"]);
        REQUIRE(call_result.content.empty() == false);
        REQUIRE(std::holds_alternative<TextContent>(call_result.content[0]));

        auto text = std::get<TextContent>(call_result.content[0]);
        REQUIRE(text.text.find("MCP workflow test content") != std::string::npos);
    }

    // 5. List resources
    auto resources_response = client.request("resources/list");
    REQUIRE(resources_response.has_value());
    auto resources = ListResourcesResult::from_json((*resources_response)["result"]);
    INFO("Available resources: " << resources.resources.size());

    // Clean up
    std::filesystem::remove(workflow_file);
    transport.stop();

    INFO("Full MCP workflow completed successfully!");
}

// ═══════════════════════════════════════════════════════════════════════════
// Async Integration Tests
// ═══════════════════════════════════════════════════════════════════════════
// Tests using AsyncMcpClient + AsyncProcessTransport with real MCP servers.

#include "mcpp/async/async_mcp_client.hpp"
#include "mcpp/async/async_process_transport.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <future>

using namespace mcpp::async;

// Helper to run async tests synchronously
// Uses use_future to properly wait for the coroutine to complete
template <typename T>
T run_async(asio::io_context& io, asio::awaitable<T> coro) {
    auto future = asio::co_spawn(io, std::move(coro), asio::use_future);
    
    // Run until the future is ready
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        io.run_one();
    }
    io.restart();
    
    return future.get();
}

// Specialization for void
inline void run_async(asio::io_context& io, asio::awaitable<void> coro) {
    auto future = asio::co_spawn(io, std::move(coro), asio::use_future);
    
    // Run until the future is ready
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        io.run_one();
    }
    io.restart();
    
    future.get();
}

TEST_CASE("Async MCP filesystem server - connect and initialize", "[real][async]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    INFO("Test directory: " << test_dir);

    asio::io_context io;

    // Create async transport
    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;  // MCP filesystem uses raw JSON
    config.stderr_handling = mcpp::async::StderrHandling::Passthrough;  // Show stderr for debugging
    config.channel_capacity = 16;

    auto transport = make_async_process_transport(io.get_executor(), config);

    // Create async client with shorter timeout for faster failure
    AsyncMcpClientConfig client_config;
    client_config.client_name = "mcpp-async-test";
    client_config.client_version = "1.0.0";
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(10);

    AsyncMcpClient client(std::move(transport), client_config);

    // Connect and initialize
    auto result = run_async(io, client.connect());

    if (result.has_value() == false) {
        FAIL("Connect error: " << result.error().message);
    }
    REQUIRE(result.has_value());
    REQUIRE(client.is_connected());
    REQUIRE(client.is_initialized());

    // Verify server info
    auto server_info = client.server_info();
    REQUIRE(server_info.has_value());
    INFO("Server: " << server_info->name << " v" << server_info->version);

    // Verify capabilities
    auto caps = client.server_capabilities();
    REQUIRE(caps.has_value());
    REQUIRE(caps->tools.has_value());  // Filesystem server has tools

    // Disconnect
    run_async(io, client.disconnect());
    REQUIRE(client.is_connected() == false);

    INFO("Async connect/initialize test passed!");
}

TEST_CASE("Async MCP filesystem server - list tools", "[real][async]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    // Connect
    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // List tools
    auto tools_result = run_async(io, client.list_tools());
    REQUIRE(tools_result.has_value());

    auto& tools = tools_result->tools;
    INFO("Found " << tools.size() << " tools");
    REQUIRE(tools.empty() == false);

    // Check for expected tools
    bool has_read_file = false;
    bool has_write_file = false;
    for (const auto& tool : tools) {
        INFO("Tool: " << tool.name);
        if (tool.name == "read_file") has_read_file = true;
        if (tool.name == "write_file") has_write_file = true;
    }
    REQUIRE(has_read_file);
    REQUIRE(has_write_file);

    // Disconnect
    run_async(io, client.disconnect());

    INFO("Async list_tools test passed!");
}

TEST_CASE("Async MCP filesystem server - call tool", "[real][async]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    // Create a test file
    auto test_file = std::filesystem::path(test_dir) / "async_test_file.txt";
    {
        std::ofstream ofs(test_file);
        ofs << "Hello from async test!";
    }

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    // Connect
    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // Call read_file tool
    auto call_result = run_async(io, client.call_tool("read_file", {
        {"path", test_file.string()}
    }));

    REQUIRE(call_result.has_value());
    REQUIRE(call_result->content.empty() == false);
    REQUIRE(call_result->is_error == false);

    // Check content
    REQUIRE(std::holds_alternative<TextContent>(call_result->content[0]));
    auto text = std::get<TextContent>(call_result->content[0]);
    REQUIRE(text.text.find("Hello from async test!") != std::string::npos);

    // Disconnect and cleanup
    run_async(io, client.disconnect());
    std::filesystem::remove(test_file);

    INFO("Async call_tool test passed!");
}

TEST_CASE("Async MCP filesystem server - full workflow", "[real][async]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.client_name = "mcpp-async-workflow";
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    // 1. Connect
    INFO("Step 1: Connecting...");
    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());
    REQUIRE(client.is_initialized());

    // 2. List tools
    INFO("Step 2: Listing tools...");
    auto tools_result = run_async(io, client.list_tools());
    REQUIRE(tools_result.has_value());
    INFO("Found " << tools_result->tools.size() << " tools");

    // 3. Write a file
    INFO("Step 3: Writing file...");
    auto workflow_file = std::filesystem::path(test_dir) / "async_workflow_test.txt";
    auto write_result = run_async(io, client.call_tool("write_file", {
        {"path", workflow_file.string()},
        {"content", "Async workflow test content - written via AsyncMcpClient!"}
    }));
    REQUIRE(write_result.has_value());
    REQUIRE(write_result->is_error == false);

    // 4. Read the file back
    INFO("Step 4: Reading file...");
    auto read_result = run_async(io, client.call_tool("read_file", {
        {"path", workflow_file.string()}
    }));
    REQUIRE(read_result.has_value());
    REQUIRE(read_result->content.empty() == false);

    auto text = std::get<TextContent>(read_result->content[0]);
    REQUIRE(text.text.find("Async workflow test content") != std::string::npos);

    // 5. Disconnect
    INFO("Step 5: Disconnecting...");
    run_async(io, client.disconnect());
    REQUIRE(client.is_connected() == false);

    // Cleanup
    std::filesystem::remove(workflow_file);

    INFO("Async full workflow completed successfully!");
}

// ═══════════════════════════════════════════════════════════════════════════
// Ping Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Async MCP server - ping", "[real][async][ping]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(10);

    AsyncMcpClient client(std::move(transport), client_config);

    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // Test ping
    auto ping_result = run_async(io, client.ping());
    
    // Note: Not all servers implement ping - it's optional
    if (ping_result.has_value()) {
        INFO("Ping successful!");
    } else {
        INFO("Ping not supported by server: " << ping_result.error().message);
    }

    run_async(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// Notification Handler Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Async MCP server - notification handlers", "[real][async][notifications]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    // Track notifications
    std::vector<std::string> notifications_received;
    client.on_notification([&](const std::string& method, const Json&) {
        notifications_received.push_back(method);
    });

    bool tool_list_changed = false;
    client.on_tool_list_changed([&]() {
        tool_list_changed = true;
    });

    bool resource_list_changed = false;
    client.on_resource_list_changed([&]() {
        resource_list_changed = true;
    });

    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // Do some operations that might trigger notifications
    auto tools_result = run_async(io, client.list_tools());
    REQUIRE(tools_result.has_value());

    // Resources may not be supported by all servers
    auto resources_result = run_async(io, client.list_resources());
    if (resources_result.has_value()) {
        INFO("Resources supported: " << resources_result->resources.size() << " resources");
    } else {
        INFO("Resources not supported by server: " << resources_result.error().message);
    }

    INFO("Notifications received: " << notifications_received.size());
    for (const auto& n : notifications_received) {
        INFO("  - " << n);
    }

    run_async(io, client.disconnect());
    
    // Note: Filesystem server may or may not send list_changed notifications
    INFO("Notification handlers test completed");
}

// ═══════════════════════════════════════════════════════════════════════════
// Resource Operations Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Async MCP filesystem server - list resources with files", "[real][async][resources]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    // Create multiple test files
    std::vector<std::filesystem::path> test_files;
    for (int i = 1; i <= 3; ++i) {
        auto path = std::filesystem::path(test_dir) / ("resource_test_" + std::to_string(i) + ".txt");
        std::ofstream(path) << "Test content " << i;
        test_files.push_back(path);
    }

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // Check if server supports resources
    auto caps = client.server_capabilities();
    if (!caps.has_value() || !caps->resources.has_value()) {
        INFO("Server does not support resources capability");
        run_async(io, client.disconnect());
        for (const auto& f : test_files) {
            std::filesystem::remove(f);
        }
        SKIP("Server does not support resources");
    }

    // List resources
    auto resources_result = run_async(io, client.list_resources());
    if (!resources_result.has_value()) {
        INFO("list_resources failed: " << resources_result.error().message);
        run_async(io, client.disconnect());
        for (const auto& f : test_files) {
            std::filesystem::remove(f);
        }
        SKIP("Server returned error for list_resources");
    }

    INFO("Found " << resources_result->resources.size() << " resources");
    for (const auto& r : resources_result->resources) {
        INFO("  - " << r.uri << " (" << r.name << ")");
    }

    // Should have at least our test files
    REQUIRE(resources_result->resources.size() >= 3);

    // Read a resource
    if (!resources_result->resources.empty()) {
        auto& first_resource = resources_result->resources[0];
        auto read_result = run_async(io, client.read_resource(first_resource.uri));
        
        if (read_result.has_value()) {
            INFO("Read resource: " << first_resource.uri);
            REQUIRE(read_result->contents.empty() == false);
        } else {
            INFO("Could not read resource: " << read_result.error().message);
        }
    }

    run_async(io, client.disconnect());

    // Cleanup
    for (const auto& f : test_files) {
        std::filesystem::remove(f);
    }

    INFO("Resource operations test completed");
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Async MCP server - error handling for invalid tool", "[real][async][errors]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // Try to call a non-existent tool
    auto call_result = run_async(io, client.call_tool("nonexistent_tool_12345", {}));
    
    // Should get an error (either from server or client)
    if (call_result.has_value()) {
        // Server might return is_error = true
        INFO("Call returned with is_error=" << call_result->is_error);
        REQUIRE(call_result->is_error == true);
    } else {
        // Or it might return an error result
        INFO("Call failed with: " << call_result.error().message);
        REQUIRE(call_result.has_value() == false);
    }

    run_async(io, client.disconnect());
}

TEST_CASE("Async MCP server - error handling for invalid file path", "[real][async][errors]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // Try to read a non-existent file
    auto call_result = run_async(io, client.call_tool("read_file", {
        {"path", "/nonexistent/path/to/file.txt"}
    }));
    
    // Should get an error
    if (call_result.has_value()) {
        INFO("Call returned with is_error=" << call_result->is_error);
        // Either is_error is true, or content contains error message
        if (call_result->is_error == false && !call_result->content.empty()) {
            if (std::holds_alternative<TextContent>(call_result->content[0])) {
                auto text = std::get<TextContent>(call_result->content[0]);
                INFO("Content: " << text.text);
            }
        }
    } else {
        INFO("Call failed with: " << call_result.error().message);
    }

    run_async(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// Progress Token Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Async MCP server - tool call with progress token", "[real][async][progress]") {
    if (is_npx_available() == false) {
        SKIP("npx not available - skipping real server test");
    }

    auto test_dir = get_test_directory();
    asio::io_context io;

    // Create a test file
    auto test_file = std::filesystem::path(test_dir) / "progress_test.txt";
    std::ofstream(test_file) << "Progress test content";

    AsyncProcessConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", test_dir};
    config.use_content_length_framing = false;
    config.stderr_handling = mcpp::async::StderrHandling::Discard;

    auto transport = make_async_process_transport(io.get_executor(), config);

    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.request_timeout = std::chrono::seconds(30);

    AsyncMcpClient client(std::move(transport), client_config);

    // Track progress notifications
    std::vector<ProgressNotification> progress_notifications;
    client.on_progress([&](const ProgressNotification& p) {
        progress_notifications.push_back(p);
    });

    auto connect_result = run_async(io, client.connect());
    REQUIRE(connect_result.has_value());

    // Call tool with progress token
    ProgressToken token = std::string("test-progress-123");
    auto call_result = run_async(io, client.call_tool("read_file", {
        {"path", test_file.string()}
    }, token));

    REQUIRE(call_result.has_value());
    
    INFO("Progress notifications received: " << progress_notifications.size());
    for (const auto& p : progress_notifications) {
        INFO("  Progress: " << p.progress << "/" << p.total.value_or(-1));
    }

    run_async(io, client.disconnect());
    std::filesystem::remove(test_file);

    INFO("Progress token test completed");
}

// ═══════════════════════════════════════════════════════════════════════════
// GitHub MCP Server Integration Tests (Local via ProcessTransport)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

bool is_github_mcp_server_available() {
    // Check if github-mcp-server is available in PATH
    return std::system("which github-mcp-server > /dev/null 2>&1") == 0;
}

bool has_github_pat() {
    const char* pat = std::getenv("GITHUB_PERSONAL_ACCESS_TOKEN");
    return pat != nullptr && std::strlen(pat) > 0;
}

}  // namespace

TEST_CASE("GitHub MCP server - server info", "[real][github]") {
    if (!is_github_mcp_server_available()) {
        SKIP("github-mcp-server not found");
    }
    if (!has_github_pat()) {
        SKIP("GITHUB_PERSONAL_ACCESS_TOKEN not set");
    }

    ProcessTransportConfig config;
    config.command = "github-mcp-server";
    config.args = {"stdio"};
    config.use_content_length_framing = false;
    config.read_timeout = std::chrono::seconds(30);

    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());

    SimpleMcpClient client(transport);

    // Initialize
    auto init_result = client.initialize("mcpp-github-test");
    REQUIRE(init_result.has_value());
    
    INFO("Server: " << init_result->server_info.name);
    INFO("Version: " << init_result->server_info.version);
    
    REQUIRE(init_result->server_info.name == "github-mcp-server");

    transport.stop();
}

TEST_CASE("GitHub MCP server - list tools", "[real][github]") {
    if (!is_github_mcp_server_available()) {
        SKIP("github-mcp-server not found");
    }
    if (!has_github_pat()) {
        SKIP("GITHUB_PERSONAL_ACCESS_TOKEN not set");
    }

    ProcessTransportConfig config;
    config.command = "github-mcp-server";
    config.args = {"stdio"};
    config.use_content_length_framing = false;
    config.read_timeout = std::chrono::seconds(30);

    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());

    SimpleMcpClient client(transport);

    auto init_result = client.initialize("mcpp-github-test");
    REQUIRE(init_result.has_value());

    // List tools
    auto tools_response = client.request("tools/list");
    REQUIRE(tools_response.has_value());
    
    if (tools_response->contains("result")) {
        auto result = (*tools_response)["result"];
        if (result.contains("tools")) {
            auto tools = result["tools"];
            INFO("Found " << tools.size() << " tools");
            REQUIRE(tools.size() > 0);
            
            // Check for expected tools
            bool has_get_me = false;
            bool has_search_repositories = false;
            for (const auto& tool : tools) {
                std::string name = tool["name"].get<std::string>();
                if (name == "get_me") has_get_me = true;
                if (name == "search_repositories") has_search_repositories = true;
            }
            REQUIRE(has_get_me);
            INFO("Has search_repositories: " << has_search_repositories);
        }
    }

    transport.stop();
}

TEST_CASE("GitHub MCP server - call get_me tool", "[real][github]") {
    if (!is_github_mcp_server_available()) {
        SKIP("github-mcp-server not found");
    }
    if (!has_github_pat()) {
        SKIP("GITHUB_PERSONAL_ACCESS_TOKEN not set");
    }

    ProcessTransportConfig config;
    config.command = "github-mcp-server";
    config.args = {"stdio"};
    config.use_content_length_framing = false;
    config.read_timeout = std::chrono::seconds(30);

    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());

    SimpleMcpClient client(transport);

    auto init_result = client.initialize("mcpp-github-test");
    REQUIRE(init_result.has_value());

    // Call get_me tool
    auto me_response = client.request("tools/call", {
        {"name", "get_me"},
        {"arguments", Json::object()}
    });
    REQUIRE(me_response.has_value());
    
    INFO("get_me response: " << me_response->dump(2));
    
    REQUIRE(me_response->contains("result"));
    auto result = (*me_response)["result"];
    
    // Should have content with user info
    REQUIRE(result.contains("content"));
    REQUIRE(result["content"].is_array());
    REQUIRE(result["content"].size() > 0);
    
    auto content = result["content"][0];
    REQUIRE(content.contains("text"));
    
    // Parse the JSON text to verify it's valid user data
    std::string text = content["text"].get<std::string>();
    auto user_json = Json::parse(text);
    INFO("User login: " << user_json.value("login", "unknown"));
    REQUIRE(user_json.contains("login"));

    transport.stop();
}

TEST_CASE("GitHub MCP server - list prompts", "[real][github]") {
    if (!is_github_mcp_server_available()) {
        SKIP("github-mcp-server not found");
    }
    if (!has_github_pat()) {
        SKIP("GITHUB_PERSONAL_ACCESS_TOKEN not set");
    }

    ProcessTransportConfig config;
    config.command = "github-mcp-server";
    config.args = {"stdio"};
    config.use_content_length_framing = false;
    config.read_timeout = std::chrono::seconds(30);

    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());

    SimpleMcpClient client(transport);

    auto init_result = client.initialize("mcpp-github-test");
    REQUIRE(init_result.has_value());

    // Check if server supports prompts
    if (!init_result->capabilities.prompts.has_value()) {
        INFO("Server does not advertise prompts capability");
        transport.stop();
        return;
    }

    // List prompts
    auto prompts_response = client.request("prompts/list");
    REQUIRE(prompts_response.has_value());
    
    INFO("Prompts response: " << prompts_response->dump(2));
    
    if (prompts_response->contains("result")) {
        auto result = (*prompts_response)["result"];
        if (result.contains("prompts")) {
            auto prompts = result["prompts"];
            INFO("Found " << prompts.size() << " prompts");
            for (const auto& prompt : prompts) {
                INFO("  - " << prompt["name"].get<std::string>());
            }
        }
    }

    transport.stop();
}

TEST_CASE("GitHub MCP server - ping", "[real][github]") {
    if (!is_github_mcp_server_available()) {
        SKIP("github-mcp-server not found");
    }
    if (!has_github_pat()) {
        SKIP("GITHUB_PERSONAL_ACCESS_TOKEN not set");
    }

    ProcessTransportConfig config;
    config.command = "github-mcp-server";
    config.args = {"stdio"};
    config.use_content_length_framing = false;
    config.read_timeout = std::chrono::seconds(30);

    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());

    SimpleMcpClient client(transport);

    auto init_result = client.initialize("mcpp-github-test");
    REQUIRE(init_result.has_value());

    // Ping
    auto ping_response = client.request("ping");
    REQUIRE(ping_response.has_value());
    
    INFO("Ping response: " << ping_response->dump(2));
    
    // Ping should return an empty result or just acknowledge
    REQUIRE(ping_response->contains("result"));

    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// HTTP Transport Integration Tests (GitHub Copilot MCP - Remote)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

std::optional<std::string> get_github_mcp_pat() {
    const char* pat = std::getenv("GITHUB_MCP_PAT");
    if (pat == nullptr || std::strlen(pat) == 0) {
        return std::nullopt;
    }
    return std::string(pat);
}

}  // namespace

TEST_CASE("HTTP Transport - GitHub Copilot MCP server info", "[real][http][github-remote]") {
    auto pat = get_github_mcp_pat();
    if (!pat) {
        SKIP("GITHUB_MCP_PAT not set - skipping HTTP transport test");
    }

    HttpTransportConfig config;
    config.base_url = "https://api.githubcopilot.com/mcp/";
    config.with_bearer_token(*pat);
    config.request_timeout = std::chrono::seconds(30);

    HttpTransport transport(config);
    transport.start();

    // Send initialize request
    Json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", Json::object()},
            {"clientInfo", {
                {"name", "mcpp-test"},
                {"version", "1.0.0"}
            }}
        }}
    };

    auto send_result = transport.send(init_request);
    REQUIRE(send_result.has_value());

    auto response = transport.receive_with_timeout(std::chrono::seconds(30));
    REQUIRE(response.has_value());
    REQUIRE(response->has_value());  // Not a timeout

    auto& resp_json = **response;
    INFO("Response: " << resp_json.dump(2));
    REQUIRE(resp_json.contains("result"));
    
    auto result = resp_json["result"];
    REQUIRE(result.contains("serverInfo"));
    
    auto server_info = result["serverInfo"];
    INFO("Server: " << server_info["name"].get<std::string>());
    INFO("Version: " << server_info.value("version", "unknown"));
    
    // Check session ID - should have one after successful init
    auto session_id = transport.session_id();
    INFO("Session ID: " << (session_id ? *session_id : "none"));
    INFO("Session state: " << static_cast<int>(transport.session_state()));
    REQUIRE(session_id.has_value());  // Server should return a session ID

    // Send initialized notification
    Json initialized = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    transport.send(initialized);

    transport.stop();
}

TEST_CASE("HTTP Transport - GitHub Copilot MCP list tools", "[real][http][github-remote]") {
    auto pat = get_github_mcp_pat();
    if (!pat) {
        SKIP("GITHUB_MCP_PAT not set - skipping HTTP transport test");
    }

    HttpTransportConfig config;
    config.base_url = "https://api.githubcopilot.com/mcp/";
    config.with_bearer_token(*pat);
    config.request_timeout = std::chrono::seconds(30);

    HttpTransport transport(config);
    transport.start();

    // Initialize first
    Json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", Json::object()},
            {"clientInfo", {{"name", "mcpp-test"}, {"version", "1.0.0"}}}
        }}
    };

    auto send_result = transport.send(init_request);
    REQUIRE(send_result.has_value());

    auto init_response = transport.receive_with_timeout(std::chrono::seconds(30));
    REQUIRE(init_response.has_value());
    REQUIRE(init_response->has_value());  // Not a timeout
    
    auto& init_json = **init_response;
    INFO("Init response: " << init_json.dump(2));
    REQUIRE(init_json.contains("result"));

    // Send initialized notification
    Json initialized = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
    auto notify_result = transport.send(initialized);
    if (!notify_result.has_value()) {
        INFO("Initialized notification error: " << notify_result.error().message);
    }
    // Notification may not have a response, that's OK

    // List tools
    Json list_tools = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"}
    };

    send_result = transport.send(list_tools);
    if (!send_result.has_value()) {
        auto& err = send_result.error();
        FAIL("Send error: " << err.message 
             << " (code=" << static_cast<int>(err.code) 
             << ", http_status=" << (err.http_status ? std::to_string(*err.http_status) : "none") << ")");
    }
    REQUIRE(send_result.has_value());

    auto tools_response = transport.receive_with_timeout(std::chrono::seconds(30));
    REQUIRE(tools_response.has_value());
    REQUIRE(tools_response->has_value());  // Not a timeout
    
    // Print tools found
    auto& tools_json = **tools_response;  // Unwrap optional<Json>
    if (tools_json.contains("result")) {
        auto result = tools_json["result"];
        if (result.contains("tools")) {
            auto tools = result["tools"];
            WARN("Found " << tools.size() << " tools on GitHub Copilot MCP:");
            for (const auto& tool : tools) {
                WARN("  - " << tool["name"].get<std::string>());
            }
            REQUIRE(tools.size() > 0);
        }
    }

    transport.stop();
}

TEST_CASE("HTTP Transport - GitHub Copilot MCP call tool", "[real][http][github-remote]") {
    auto pat = get_github_mcp_pat();
    if (!pat) {
        SKIP("GITHUB_MCP_PAT not set - skipping HTTP transport test");
    }

    HttpTransportConfig config;
    config.base_url = "https://api.githubcopilot.com/mcp/";
    config.with_bearer_token(*pat);
    config.request_timeout = std::chrono::seconds(30);

    HttpTransport transport(config);
    transport.start();

    // Initialize
    Json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", Json::object()},
            {"clientInfo", {{"name", "mcpp-test"}, {"version", "1.0.0"}}}
        }}
    };

    transport.send(init_request);
    auto init_response = transport.receive_with_timeout(std::chrono::seconds(30));
    REQUIRE(init_response.has_value());
    REQUIRE(init_response->has_value());

    Json initialized = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
    transport.send(initialized);

    // Call get_me tool (if available)
    Json call_tool = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/call"},
        {"params", {
            {"name", "get_me"},
            {"arguments", Json::object()}
        }}
    };

    auto send_result = transport.send(call_tool);
    REQUIRE(send_result.has_value());

    auto tool_response = transport.receive_with_timeout(std::chrono::seconds(30));
    REQUIRE(tool_response.has_value());
    REQUIRE(tool_response->has_value());  // Not a timeout
    
    auto& tool_json = **tool_response;
    INFO("Tool response: " << tool_json.dump(2));
    
    // Should get either a result or an error (if tool not available)
    bool has_result = tool_json.contains("result");
    bool has_error = tool_json.contains("error");
    REQUIRE((has_result || has_error));
    
    if (has_result) {
        auto result = tool_json["result"];
        INFO("Tool call succeeded");
        if (result.contains("content") && !result["content"].empty()) {
            auto content = result["content"][0];
            if (content.contains("text")) {
                INFO("Content: " << content["text"].get<std::string>());
            }
        }
    } else {
        INFO("Tool call returned error: " << tool_json["error"]["message"].get<std::string>());
    }

    transport.stop();
}

