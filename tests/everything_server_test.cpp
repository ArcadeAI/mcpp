// Comprehensive tests against the Everything MCP server
// Tests both stdio and HTTP transports, sync and async clients, and handlers
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "mcpp/transport/process_transport.hpp"
#include "mcpp/transport/http_transport.hpp"
#include "mcpp/async/async_mcp_client.hpp"
#include "mcpp/async/async_process_transport.hpp"
#include "mcpp/client/sampling_handler.hpp"
#include "mcpp/client/elicitation_handler.hpp"
#include "mcpp/client/roots_handler.hpp"
#include "mcpp/protocol/mcp_types.hpp"

#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

#include <thread>
#include <chrono>
#include <atomic>
#include <set>
#include <iostream>

using namespace mcpp;
using namespace mcpp::async;

// ═══════════════════════════════════════════════════════════════════════════
// Test Handlers
// ═══════════════════════════════════════════════════════════════════════════

class TestSamplingHandler : public ISamplingHandler {
public:
    std::atomic<int> call_count{0};
    std::string last_prompt;
    
    std::optional<CreateMessageResult> handle_create_message(
        const CreateMessageParams& params
    ) override {
        call_count++;
        if (!params.messages.empty()) {
            for (const auto& msg : params.messages) {
                if (auto* text = std::get_if<TextContent>(&msg.content)) {
                    last_prompt = text->text;
                }
            }
        }
        
        CreateMessageResult result;
        result.role = SamplingRole::Assistant;
        result.model = "test-model";
        result.stop_reason = StopReason::EndTurn;
        TextContent content;
        content.text = "Test response from sampling handler";
        result.content = content;
        return result;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Helper to run async code with timeout
// ═══════════════════════════════════════════════════════════════════════════

template<typename T>
T run_with_timeout(asio::io_context& io, asio::awaitable<T> awaitable, 
                   std::chrono::seconds timeout = std::chrono::seconds(10)) {
    std::optional<T> result;
    std::exception_ptr ex;
    bool done = false;
    
    asio::co_spawn(io, 
        [&]() -> asio::awaitable<void> {
            try {
                result = co_await std::move(awaitable);
            } catch (...) {
                ex = std::current_exception();
            }
            done = true;
        },
        asio::detached
    );
    
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done && std::chrono::steady_clock::now() < deadline) {
        io.run_for(std::chrono::milliseconds(50));
    }
    
    if (!done) {
        throw std::runtime_error("Operation timed out");
    }
    
    if (ex) std::rethrow_exception(ex);
    return std::move(*result);
}

inline void run_void_with_timeout(asio::io_context& io, asio::awaitable<void> awaitable,
                                  std::chrono::seconds timeout = std::chrono::seconds(10)) {
    std::exception_ptr ex;
    bool done = false;
    
    asio::co_spawn(io, 
        [&]() -> asio::awaitable<void> {
            try {
                co_await std::move(awaitable);
            } catch (...) {
                ex = std::current_exception();
            }
            done = true;
        },
        asio::detached
    );
    
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done && std::chrono::steady_clock::now() < deadline) {
        io.run_for(std::chrono::milliseconds(50));
    }
    
    if (!done) {
        throw std::runtime_error("Operation timed out");
    }
    
    if (ex) std::rethrow_exception(ex);
}

// ═══════════════════════════════════════════════════════════════════════════
// Stdio Transport Tests (Sync - Low-level) - Single comprehensive test
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - sync stdio - comprehensive", "[everything][stdio][sync]") {
    ProcessTransportConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    int next_id = 1;
    
    // Helper to send request and get response
    auto request = [&](const std::string& method, const Json& params = Json::object()) -> Json {
        int request_id = next_id++;
        Json req = {
            {"jsonrpc", "2.0"},
            {"id", request_id},
            {"method", method}
        };
        if (!params.empty()) {
            req["params"] = params;
        }
        
        auto send_res = transport.send(req);
        REQUIRE(send_res.has_value());
        
        // Loop until we get a response with our request_id
        while (true) {
            auto msg = transport.receive();
            REQUIRE(msg.has_value());
            
            // Skip notifications (no id field)
            if (!msg->contains("id")) {
                continue;
            }
            
            // Check if this is a server request (has "method" field)
            if (msg->contains("method")) {
                // This is a server-initiated request, respond to it
                int server_req_id = (*msg)["id"].get<int>();
                std::string server_method = (*msg)["method"].get<std::string>();
                
                Json response;
                if (server_method == "roots/list") {
                    // Return empty roots
                    response = {
                        {"jsonrpc", "2.0"},
                        {"id", server_req_id},
                        {"result", {{"roots", Json::array()}}}
                    };
                } else if (server_method == "sampling/createMessage") {
                    // Return a simple response
                    response = {
                        {"jsonrpc", "2.0"},
                        {"id", server_req_id},
                        {"result", {
                            {"role", "assistant"},
                            {"content", {{"type", "text"}, {"text", "Test response"}}},
                            {"model", "test-model"},
                            {"stopReason", "endTurn"}
                        }}
                    };
                } else {
                    // Unknown request, return error
                    response = {
                        {"jsonrpc", "2.0"},
                        {"id", server_req_id},
                        {"error", {{"code", -32601}, {"message", "Method not found"}}}
                    };
                }
                (void)transport.send(response);
                continue;
            }
            
            // Check if this is our response
            if ((*msg)["id"].get<int>() == request_id) {
                return *msg;
            }
            
            // Some other response, skip
        }
    };
    
    // Initialize
    Json init_response = request("initialize", {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {{"roots", Json::object()}}},
        {"clientInfo", {{"name", "mcpp-test"}, {"version", "1.0.0"}}}
    });
    
    REQUIRE(init_response.contains("result"));
    auto server_info = init_response["result"]["serverInfo"];
    INFO("Server: " << server_info["name"].get<std::string>());
    REQUIRE(server_info["name"].get<std::string>() == "example-servers/everything");
    
    // Send initialized notification
    (void)transport.send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    
    // List tools
    auto tools_response = request("tools/list");
    INFO("tools_response: " << tools_response.dump(2));
    REQUIRE(tools_response.contains("result"));
    auto tools = tools_response["result"]["tools"];
    INFO("Tools count: " << tools.size());
    REQUIRE(tools.size() >= 10);
    
    std::set<std::string> tool_names;
    for (const auto& tool : tools) {
        tool_names.insert(tool["name"].get<std::string>());
    }
    REQUIRE(tool_names.count("echo") == 1);
    REQUIRE(tool_names.count("add") == 1);
    
    // List prompts
    auto prompts_response = request("prompts/list");
    REQUIRE(prompts_response.contains("result"));
    auto prompts = prompts_response["result"]["prompts"];
    INFO("Prompts count: " << prompts.size());
    REQUIRE(prompts.size() >= 2);
    
    // List resources
    auto resources_response = request("resources/list");
    REQUIRE(resources_response.contains("result"));
    auto resources = resources_response["result"]["resources"];
    INFO("Resources count: " << resources.size());
    REQUIRE(resources.size() >= 10);
    
    // Call echo tool
    auto echo_response = request("tools/call", {{"name", "echo"}, {"arguments", {{"message", "Hello mcpp!"}}}});
    REQUIRE(echo_response.contains("result"));
    auto echo_content = echo_response["result"]["content"];
    REQUIRE(echo_content.size() > 0);
    REQUIRE(echo_content[0]["text"].get<std::string>().find("Hello mcpp!") != std::string::npos);
    
    // Call add tool
    auto add_response = request("tools/call", {{"name", "add"}, {"arguments", {{"a", 10}, {"b", 32}}}});
    REQUIRE(add_response.contains("result"));
    auto add_content = add_response["result"]["content"];
    REQUIRE(add_content.size() > 0);
    REQUIRE(add_content[0]["text"].get<std::string>().find("42") != std::string::npos);
    
    // Ping
    auto ping_response = request("ping");
    REQUIRE(ping_response.contains("result"));
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Async Stdio Transport Tests - Comprehensive
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - async stdio - comprehensive", "[everything][stdio][async]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    auto transport = std::make_unique<AsyncProcessTransport>(io.get_executor(), config);
    
    // Set up a roots handler so the server's roots/list request is handled
    auto roots_handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{});
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_name = "mcpp-async-test";
    client_config.client_version = "1.0.0";
    client_config.capabilities.roots = ClientCapabilities::Roots{};
    client_config.request_timeout = std::chrono::seconds(10);
    
    AsyncMcpClient client(std::move(transport), client_config);
    client.set_roots_handler(roots_handler);
    
    // Connect and initialize
    auto connect_result = run_with_timeout(io, client.connect());
    REQUIRE(connect_result.has_value());
    INFO("Server: " << connect_result->server_info.name);
    REQUIRE(connect_result->server_info.name == "example-servers/everything");
    
    // List tools
    auto tools_result = run_with_timeout(io, client.list_tools());
    REQUIRE(tools_result.has_value());
    INFO("Tools count: " << tools_result->tools.size());
    REQUIRE(tools_result->tools.size() >= 10);
    
    // List prompts
    auto prompts_result = run_with_timeout(io, client.list_prompts());
    REQUIRE(prompts_result.has_value());
    INFO("Prompts count: " << prompts_result->prompts.size());
    REQUIRE(prompts_result->prompts.size() >= 2);
    
    // List resources
    auto resources_result = run_with_timeout(io, client.list_resources());
    REQUIRE(resources_result.has_value());
    INFO("Resources count: " << resources_result->resources.size());
    REQUIRE(resources_result->resources.size() >= 10);
    
    // Call tool
    auto echo_result = run_with_timeout(io, client.call_tool("echo", {{"message", "Async hello!"}}));
    REQUIRE(echo_result.has_value());
    REQUIRE(echo_result->content.size() > 0);
    if (auto* text_content = std::get_if<TextContent>(&echo_result->content[0])) {
        REQUIRE(text_content->text.find("Async hello!") != std::string::npos);
    }
    
    // Ping
    auto ping_result = run_with_timeout(io, client.ping());
    REQUIRE(ping_result.has_value());
    
    run_void_with_timeout(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// HTTP/Streamable Transport Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - HTTP/Streamable transport", "[everything][http][!mayfail]") {
    // Requires: npx -y @modelcontextprotocol/server-everything streamableHttp
    // Server runs on port 3001, endpoint is /mcp
    
    HttpTransportConfig config;
    config.base_url = "http://localhost:3001/mcp";
    config.request_timeout = std::chrono::seconds(10);
    
    HttpTransport transport(config);
    auto start_result = transport.start();
    if (!start_result.has_value()) {
        SKIP("Could not start transport: " + start_result.error().message);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    Json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", {{"roots", Json::object()}}},
            {"clientInfo", {{"name", "mcpp-http-test"}, {"version", "1.0.0"}}}
        }}
    };
    
    auto send_result = transport.send(init_request);
    REQUIRE(send_result.has_value());
    
    auto response = transport.receive_with_timeout(std::chrono::seconds(10));
    REQUIRE(response.has_value());
    REQUIRE(response->has_value());
    
    auto& resp_json = **response;
    REQUIRE(resp_json.contains("result"));
    
    auto server_info = resp_json["result"]["serverInfo"];
    INFO("Server: " << server_info["name"].get<std::string>());
    REQUIRE(server_info["name"].get<std::string>() == "example-servers/everything");
    
    (void)transport.send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    
    // List tools
    (void)transport.send({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    
    auto tools_response = transport.receive_with_timeout(std::chrono::seconds(10));
    REQUIRE(tools_response.has_value());
    REQUIRE(tools_response->has_value());
    
    auto& tools_json = **tools_response;
    REQUIRE(tools_json.contains("result"));
    auto tools = tools_json["result"]["tools"];
    INFO("Tools count: " << tools.size());
    REQUIRE(tools.size() > 0);
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Handler Tests - Sampling
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - sampling handler", "[everything][handlers][sampling]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    auto transport = std::make_unique<AsyncProcessTransport>(io.get_executor(), config);
    
    auto sampling_handler = std::make_shared<TestSamplingHandler>();
    auto roots_handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{});
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_name = "mcpp-handler-test";
    client_config.client_version = "1.0.0";
    client_config.capabilities.sampling = ClientCapabilities::Sampling{};
    client_config.capabilities.roots = ClientCapabilities::Roots{};
    client_config.request_timeout = std::chrono::seconds(15);
    
    AsyncMcpClient client(std::move(transport), client_config);
    client.set_sampling_handler(sampling_handler);
    client.set_roots_handler(roots_handler);
    
    auto connect_result = run_with_timeout(io, client.connect());
    REQUIRE(connect_result.has_value());
    INFO("Server: " << connect_result->server_info.name);
    
    // sampleLLM tool triggers sampling request
    auto result = run_with_timeout(io, client.call_tool("sampleLLM", {
        {"prompt", "What is 2+2?"},
        {"maxTokens", 50}
    }), std::chrono::seconds(15));
    
    INFO("Sampling handler call count: " << sampling_handler->call_count.load());
    // Handler should have been called if server supports sampling
    
    run_void_with_timeout(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// Handler Tests - Roots
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - roots handler", "[everything][handlers][roots]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    auto transport = std::make_unique<AsyncProcessTransport>(io.get_executor(), config);
    
    auto roots_handler = std::make_shared<MutableRootsHandler>();
    roots_handler->add_root(Root{"/tmp", "Temp Directory"});
    roots_handler->add_root(Root{"/home", "Home Directory"});
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_name = "mcpp-roots-test";
    client_config.client_version = "1.0.0";
    client_config.capabilities.roots = ClientCapabilities::Roots{true};
    client_config.request_timeout = std::chrono::seconds(10);
    
    AsyncMcpClient client(std::move(transport), client_config);
    client.set_roots_handler(roots_handler);
    
    auto connect_result = run_with_timeout(io, client.connect());
    REQUIRE(connect_result.has_value());
    
    auto tools_result = run_with_timeout(io, client.list_tools());
    REQUIRE(tools_result.has_value());
    
    bool has_list_roots = false;
    for (const auto& tool : tools_result->tools) {
        if (tool.name == "listRoots") {
            has_list_roots = true;
            break;
        }
    }
    
    if (has_list_roots) {
        INFO("Testing listRoots tool");
        auto result = run_with_timeout(io, client.call_tool("listRoots", {}));
        INFO("listRoots: " << (result.has_value() ? "success" : result.error().message));
    }
    
    // Test notify_roots_changed
    roots_handler->add_root(Root{"/var", "Var Directory"});
    auto notify_result = run_with_timeout(io, client.notify_roots_changed());
    INFO("notify_roots_changed: " << (notify_result.has_value() ? "success" : notify_result.error().message));
    
    run_void_with_timeout(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// Prompts Test
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - prompts", "[everything][prompts]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    auto transport = std::make_unique<AsyncProcessTransport>(io.get_executor(), config);
    auto roots_handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{});
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_name = "mcpp-prompts-test";
    client_config.client_version = "1.0.0";
    client_config.capabilities.roots = ClientCapabilities::Roots{};
    client_config.request_timeout = std::chrono::seconds(10);
    
    AsyncMcpClient client(std::move(transport), client_config);
    client.set_roots_handler(roots_handler);
    
    auto connect_result = run_with_timeout(io, client.connect());
    REQUIRE(connect_result.has_value());
    
    // List prompts
    auto prompts_result = run_with_timeout(io, client.list_prompts());
    REQUIRE(prompts_result.has_value());
    REQUIRE(prompts_result->prompts.size() >= 2);
    
    // Find and test simple_prompt
    bool found_simple = false;
    for (const auto& prompt : prompts_result->prompts) {
        if (prompt.name == "simple_prompt") {
            found_simple = true;
            INFO("Found simple_prompt: " << prompt.description.value_or("no description"));
            break;
        }
    }
    REQUIRE(found_simple);
    
    // Get a prompt
    auto get_result = run_with_timeout(io, client.get_prompt("simple_prompt", {}));
    REQUIRE(get_result.has_value());
    REQUIRE(get_result->messages.size() > 0);
    INFO("Prompt messages: " << get_result->messages.size());
    
    run_void_with_timeout(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// Resources Test
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - resources", "[everything][resources]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    auto transport = std::make_unique<AsyncProcessTransport>(io.get_executor(), config);
    auto roots_handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{});
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_name = "mcpp-resources-test";
    client_config.client_version = "1.0.0";
    client_config.capabilities.roots = ClientCapabilities::Roots{};
    client_config.request_timeout = std::chrono::seconds(10);
    
    AsyncMcpClient client(std::move(transport), client_config);
    client.set_roots_handler(roots_handler);
    
    auto connect_result = run_with_timeout(io, client.connect());
    REQUIRE(connect_result.has_value());
    
    // List resources
    auto resources_result = run_with_timeout(io, client.list_resources());
    REQUIRE(resources_result.has_value());
    REQUIRE(resources_result->resources.size() >= 10);
    INFO("Resources count: " << resources_result->resources.size());
    
    // Read a resource
    if (!resources_result->resources.empty()) {
        const auto& first_resource = resources_result->resources[0];
        INFO("Reading resource: " << first_resource.uri);
        
        auto read_result = run_with_timeout(io, client.read_resource(first_resource.uri));
        REQUIRE(read_result.has_value());
        REQUIRE(read_result->contents.size() > 0);
    }
    
    // List resource templates
    auto templates_result = run_with_timeout(io, client.list_resource_templates());
    INFO("Resource templates: " << (templates_result.has_value() ? std::to_string(templates_result->resource_templates.size()) : templates_result.error().message));
    
    run_void_with_timeout(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation Test (if supported)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - elicitation check", "[everything][elicitation][!mayfail]") {
    // Note: Everything server may not support elicitation
    // This test checks if the capability is advertised and handler is set up
    
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    auto transport = std::make_unique<AsyncProcessTransport>(io.get_executor(), config);
    
    class TestElicitationHandler : public IElicitationHandler {
    public:
        std::atomic<int> call_count{0};
        
        ElicitationResult handle_form(
            const std::string& message,
            const Json& schema
        ) override {
            call_count++;
            (void)message;
            (void)schema;
            return {ElicitationAction::Accept, Json{{"response", "test"}}};
        }
        
        ElicitationResult handle_url(
            const std::string& elicitation_id,
            const std::string& url,
            const std::string& message
        ) override {
            call_count++;
            (void)elicitation_id;
            (void)url;
            (void)message;
            return {ElicitationAction::Opened, std::nullopt};
        }
    };
    
    auto elicitation_handler = std::make_shared<TestElicitationHandler>();
    auto roots_handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{});
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_name = "mcpp-elicitation-test";
    client_config.client_version = "1.0.0";
    client_config.capabilities.elicitation = ElicitationCapability{true, true};
    client_config.capabilities.roots = ClientCapabilities::Roots{};
    client_config.request_timeout = std::chrono::seconds(10);
    
    AsyncMcpClient client(std::move(transport), client_config);
    client.set_elicitation_handler(elicitation_handler);
    client.set_roots_handler(roots_handler);
    
    auto connect_result = run_with_timeout(io, client.connect());
    REQUIRE(connect_result.has_value());
    
    // List tools to see if there's an elicitation-triggering tool
    auto tools_result = run_with_timeout(io, client.list_tools());
    REQUIRE(tools_result.has_value());
    
    bool found_elicit_tool = false;
    for (const auto& tool : tools_result->tools) {
        if (tool.name.find("elicit") != std::string::npos) {
            found_elicit_tool = true;
            INFO("Found elicitation tool: " << tool.name);
        }
    }
    
    INFO("Server has elicitation tool: " << found_elicit_tool);
    INFO("Elicitation handler call count: " << elicitation_handler->call_count.load());
    
    run_void_with_timeout(io, client.disconnect());
}

// ═══════════════════════════════════════════════════════════════════════════
// Progress Notifications Test
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Everything server - progress notifications", "[everything][notifications][progress]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "mcp-server-everything";
    config.args = {};
    config.use_content_length_framing = false;
    
    auto transport = std::make_unique<AsyncProcessTransport>(io.get_executor(), config);
    
    std::atomic<int> progress_count{0};
    auto roots_handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{});
    
    AsyncMcpClientConfig client_config;
    client_config.auto_initialize = true;
    client_config.client_name = "mcpp-progress-test";
    client_config.client_version = "1.0.0";
    client_config.capabilities.roots = ClientCapabilities::Roots{};
    client_config.request_timeout = std::chrono::seconds(15);
    
    AsyncMcpClient client(std::move(transport), client_config);
    client.set_roots_handler(roots_handler);
    
    client.on_progress([&](const ProgressNotification& p) {
        progress_count++;
        (void)p;
    });
    
    auto connect_result = run_with_timeout(io, client.connect());
    REQUIRE(connect_result.has_value());
    
    // longRunningOperation with progress token
    auto result = run_with_timeout(io, client.call_tool("longRunningOperation", {
        {"duration", 2},
        {"steps", 3}
    }, ProgressToken{"test-progress-123"}), std::chrono::seconds(15));
    
    INFO("Progress notifications: " << progress_count.load());
    
    run_void_with_timeout(io, client.disconnect());
}
