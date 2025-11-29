// ─────────────────────────────────────────────────────────────────────────────
// mcpp-cli - MCP Server Testing Tool
// ─────────────────────────────────────────────────────────────────────────────
// A command-line interface for testing and interacting with MCP servers.
//
// Usage:
//   # Stdio transport (local server)
//   mcpp-cli --command "npx -y @modelcontextprotocol/server-filesystem /tmp"
//   mcpp-cli --command "python mcp_server.py" --list-tools
//
//   # HTTP transport (remote server / Arcade gateway)
//   mcpp-cli --url "https://api.arcade.dev/mcp/my-gateway" \
//            --header "Authorization: Bearer arc_xxx" \
//            --header "Arcade-User-ID: user@example.com" \
//            --list-tools
//
//   # Arcade shorthand
//   mcpp-cli --arcade my-gateway --arcade-key arc_xxx --arcade-user user@example.com
//
// Features:
//   - Connect to any MCP server via stdio or HTTP transport
//   - Native Arcade AI gateway support with --arcade flag
//   - List tools, resources, prompts, and resource templates
//   - Call tools with JSON arguments
//   - Read resources by URI
//   - Interactive REPL mode
//   - JSON output for scripting

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/transport/process_transport.hpp"
#include "mcpp/transport/http_transport.hpp"
#include "mcpp/protocol/mcp_types.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <thread>
#include <memory>
#include <cstdlib>

using namespace mcpp;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// ANSI Color Codes
// ═══════════════════════════════════════════════════════════════════════════

namespace color {
    const char* reset   = "\033[0m";
    const char* bold    = "\033[1m";
    const char* dim     = "\033[2m";
    const char* red     = "\033[31m";
    const char* green   = "\033[32m";
    const char* yellow  = "\033[33m";
    const char* blue    = "\033[34m";
    const char* magenta = "\033[35m";
    const char* cyan    = "\033[36m";
    
    bool enabled = true;
    
    std::string c(const char* code) {
        return enabled ? code : "";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Transport Interface
// ═══════════════════════════════════════════════════════════════════════════

// Unified interface for both stdio and HTTP transports
class ICliTransport {
public:
    virtual ~ICliTransport() = default;
    virtual TransportResult<void> send(const Json& message) = 0;
    virtual TransportResult<Json> receive() = 0;
    virtual TransportResult<void> start() = 0;
    virtual void stop() = 0;
};

// Wrapper for ProcessTransport (stdio)
class StdioCliTransport : public ICliTransport {
public:
    explicit StdioCliTransport(ProcessTransportConfig config)
        : transport_(config)
    {}
    
    TransportResult<void> send(const Json& message) override {
        return transport_.send(message);
    }
    
    TransportResult<Json> receive() override {
        return transport_.receive();
    }
    
    TransportResult<void> start() override {
        return transport_.start();
    }
    
    void stop() override {
        transport_.stop();
    }
    
private:
    ProcessTransport transport_;
};

// Wrapper for HttpTransport
class HttpCliTransport : public ICliTransport {
public:
    explicit HttpCliTransport(HttpTransportConfig config)
        : transport_(config)
    {}
    
    TransportResult<void> send(const Json& message) override {
        auto result = transport_.send(message);
        if (!result) {
            return tl::unexpected(TransportError{
                TransportError::Category::Network,
                result.error().message,
                std::nullopt
            });
        }
        return {};
    }
    
    TransportResult<Json> receive() override {
        auto result = transport_.receive();
        if (!result) {
            return tl::unexpected(TransportError{
                TransportError::Category::Network,
                result.error().message,
                std::nullopt
            });
        }
        return *result;
    }
    
    TransportResult<void> start() override {
        try {
            transport_.start();
            return {};
        } catch (const std::exception& e) {
            return tl::unexpected(TransportError{
                TransportError::Category::Network,
                std::string("Failed to start HTTP transport: ") + e.what(),
                std::nullopt
            });
        }
    }
    
    void stop() override {
        transport_.stop();
    }
    
private:
    HttpTransport transport_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Simple MCP Client (reused from tests)
// ═══════════════════════════════════════════════════════════════════════════

class CliMcpClient {
public:
    explicit CliMcpClient(ICliTransport& transport)
        : transport_(transport)
    {}

    TransportResult<Json> request(const std::string& method, const Json& params = Json::object()) {
        Json req = {
            {"jsonrpc", "2.0"},
            {"id", ++request_id_},
            {"method", method}
        };
        if (!params.empty()) {
            req["params"] = params;
        }

        auto send_result = transport_.send(req);
        if (!send_result) {
            return tl::unexpected(send_result.error());
        }

        // Keep reading until we get a response (skip notifications)
        while (true) {
            auto result = transport_.receive();
            if (!result) {
                return result;
            }
            
            // Check if this is a response (has "id" field) or notification (has "method" but no "id")
            if (result->contains("id")) {
                return result;  // This is the response we're waiting for
            }
            // Otherwise it's a notification - skip it and read again
        }
    }

    TransportResult<void> notify(const std::string& method, const Json& params = Json::object()) {
        Json notification = {
            {"jsonrpc", "2.0"},
            {"method", method}
        };
        if (!params.empty()) {
            notification["params"] = params;
        }
        return transport_.send(notification);
    }

    TransportResult<InitializeResult> initialize(const std::string& client_name = "mcpp-cli") {
        InitializeParams params;
        params.client_info = {client_name, "1.0.0"};

        auto result = request("initialize", params.to_json());
        if (!result) {
            return tl::unexpected(result.error());
        }

        if (result->contains("error")) {
            return tl::unexpected(TransportError{
                TransportError::Category::Protocol,
                (*result)["error"]["message"].get<std::string>(),
                std::nullopt
            });
        }

        auto notify_result = notify("notifications/initialized");
        if (!notify_result) {
            return tl::unexpected(notify_result.error());
        }

        return InitializeResult::from_json((*result)["result"]);
    }

private:
    ICliTransport& transport_;
    int request_id_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Output Helpers
// ═══════════════════════════════════════════════════════════════════════════

void print_error(const std::string& msg) {
    std::cerr << color::c(color::red) << "Error: " << color::c(color::reset) << msg << "\n";
}

void print_success(const std::string& msg) {
    std::cout << color::c(color::green) << "✓ " << color::c(color::reset) << msg << "\n";
}

void print_header(const std::string& title) {
    std::cout << "\n" << color::c(color::bold) << color::c(color::cyan) 
              << "═══ " << title << " ═══" << color::c(color::reset) << "\n\n";
}

void print_json(const Json& j, bool compact = false) {
    if (compact) {
        std::cout << j.dump() << "\n";
    } else {
        std::cout << j.dump(2) << "\n";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Command Handlers
// ═══════════════════════════════════════════════════════════════════════════

int cmd_list_tools(CliMcpClient& client, bool json_output) {
    auto result = client.request("tools/list");
    if (!result) {
        print_error(result.error().message);
        return 1;
    }

    if (result->contains("error")) {
        print_error((*result)["error"]["message"].get<std::string>());
        return 1;
    }

    auto tools_result = ListToolsResult::from_json((*result)["result"]);

    if (json_output) {
        Json output = Json::array();
        for (const auto& tool : tools_result.tools) {
            output.push_back(tool.to_json());
        }
        print_json(output);
    } else {
        print_header("Tools");
        if (tools_result.tools.empty()) {
            std::cout << color::c(color::dim) << "(no tools available)" << color::c(color::reset) << "\n";
        } else {
            for (const auto& tool : tools_result.tools) {
                std::cout << color::c(color::bold) << color::c(color::yellow) 
                          << "• " << tool.name << color::c(color::reset);
                if (tool.description) {
                    std::cout << "\n  " << color::c(color::dim) << *tool.description << color::c(color::reset);
                }
                if (tool.annotations) {
                    std::cout << "\n  " << color::c(color::magenta) << "[";
                    std::vector<std::string> hints;
                    if (tool.annotations->read_only_hint && *tool.annotations->read_only_hint) hints.push_back("read-only");
                    if (tool.annotations->destructive_hint && *tool.annotations->destructive_hint) hints.push_back("destructive");
                    if (tool.annotations->idempotent_hint && *tool.annotations->idempotent_hint) hints.push_back("idempotent");
                    if (tool.annotations->open_world_hint && *tool.annotations->open_world_hint) hints.push_back("open-world");
                    for (size_t i = 0; i < hints.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << hints[i];
                    }
                    std::cout << "]" << color::c(color::reset);
                }
                std::cout << "\n\n";
            }
        }
    }
    return 0;
}

int cmd_list_resources(CliMcpClient& client, bool json_output) {
    auto result = client.request("resources/list");
    if (!result) {
        print_error(result.error().message);
        return 1;
    }

    if (result->contains("error")) {
        print_error((*result)["error"]["message"].get<std::string>());
        return 1;
    }

    auto resources_result = ListResourcesResult::from_json((*result)["result"]);

    if (json_output) {
        Json output = Json::array();
        for (const auto& res : resources_result.resources) {
            output.push_back(res.to_json());
        }
        print_json(output);
    } else {
        print_header("Resources");
        if (resources_result.resources.empty()) {
            std::cout << color::c(color::dim) << "(no resources available)" << color::c(color::reset) << "\n";
        } else {
            for (const auto& res : resources_result.resources) {
                std::cout << color::c(color::bold) << color::c(color::blue) 
                          << "• " << res.name << color::c(color::reset) << "\n";
                std::cout << "  " << color::c(color::dim) << res.uri << color::c(color::reset);
                if (res.mime_type) {
                    std::cout << " (" << *res.mime_type << ")";
                }
                std::cout << "\n";
                if (res.description) {
                    std::cout << "  " << *res.description << "\n";
                }
                std::cout << "\n";
            }
        }
    }
    return 0;
}

int cmd_list_prompts(CliMcpClient& client, bool json_output) {
    auto result = client.request("prompts/list");
    if (!result) {
        print_error(result.error().message);
        return 1;
    }

    if (result->contains("error")) {
        print_error((*result)["error"]["message"].get<std::string>());
        return 1;
    }

    auto prompts_result = ListPromptsResult::from_json((*result)["result"]);

    if (json_output) {
        Json output = Json::array();
        for (const auto& prompt : prompts_result.prompts) {
            output.push_back(prompt.to_json());
        }
        print_json(output);
    } else {
        print_header("Prompts");
        if (prompts_result.prompts.empty()) {
            std::cout << color::c(color::dim) << "(no prompts available)" << color::c(color::reset) << "\n";
        } else {
            for (const auto& prompt : prompts_result.prompts) {
                std::cout << color::c(color::bold) << color::c(color::magenta) 
                          << "• " << prompt.name << color::c(color::reset);
                if (prompt.description) {
                    std::cout << "\n  " << color::c(color::dim) << *prompt.description << color::c(color::reset);
                }
                if (!prompt.arguments.empty()) {
                    std::cout << "\n  Arguments: ";
                    for (size_t i = 0; i < prompt.arguments.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << prompt.arguments[i].name;
                        if (prompt.arguments[i].required) {
                            std::cout << color::c(color::red) << "*" << color::c(color::reset);
                        }
                    }
                }
                std::cout << "\n\n";
            }
        }
    }
    return 0;
}

int cmd_list_templates(CliMcpClient& client, bool json_output) {
    auto result = client.request("resources/templates/list");
    if (!result) {
        print_error(result.error().message);
        return 1;
    }

    if (result->contains("error")) {
        print_error((*result)["error"]["message"].get<std::string>());
        return 1;
    }

    auto templates_result = ListResourceTemplatesResult::from_json((*result)["result"]);

    if (json_output) {
        Json output = Json::array();
        for (const auto& tmpl : templates_result.resource_templates) {
            output.push_back(tmpl.to_json());
        }
        print_json(output);
    } else {
        print_header("Resource Templates");
        if (templates_result.resource_templates.empty()) {
            std::cout << color::c(color::dim) << "(no resource templates available)" << color::c(color::reset) << "\n";
        } else {
            for (const auto& tmpl : templates_result.resource_templates) {
                std::cout << color::c(color::bold) << color::c(color::cyan) 
                          << "• " << tmpl.name << color::c(color::reset) << "\n";
                std::cout << "  " << color::c(color::dim) << tmpl.uri_template << color::c(color::reset);
                if (tmpl.mime_type) {
                    std::cout << " (" << *tmpl.mime_type << ")";
                }
                std::cout << "\n";
                if (tmpl.description) {
                    std::cout << "  " << *tmpl.description << "\n";
                }
                std::cout << "\n";
            }
        }
    }
    return 0;
}

int cmd_call_tool(CliMcpClient& client, const std::string& tool_name, 
                  const std::string& args_json, bool json_output) {
    Json args = Json::object();
    if (!args_json.empty()) {
        try {
            args = Json::parse(args_json);
        } catch (const Json::parse_error& e) {
            print_error("Invalid JSON arguments: " + std::string(e.what()));
            return 1;
        }
    }

    Json params = {
        {"name", tool_name},
        {"arguments", args}
    };

    auto result = client.request("tools/call", params);
    if (!result) {
        print_error(result.error().message);
        return 1;
    }

    if (result->contains("error")) {
        print_error((*result)["error"]["message"].get<std::string>());
        return 1;
    }

    auto call_result = CallToolResult::from_json((*result)["result"]);

    if (json_output) {
        print_json((*result)["result"]);
    } else {
        if (call_result.is_error) {
            print_error("Tool returned error");
        }
        
        for (const auto& content : call_result.content) {
            if (std::holds_alternative<TextContent>(content)) {
                std::cout << std::get<TextContent>(content).text << "\n";
            } else if (std::holds_alternative<ImageContent>(content)) {
                std::cout << color::c(color::dim) << "[Image: " 
                          << std::get<ImageContent>(content).mime_type << "]" 
                          << color::c(color::reset) << "\n";
            } else if (std::holds_alternative<EmbeddedResource>(content)) {
                auto& res = std::get<EmbeddedResource>(content);
                std::cout << color::c(color::dim) << "[Resource: " << res.uri << "]" 
                          << color::c(color::reset) << "\n";
            }
        }
    }
    
    return call_result.is_error ? 1 : 0;
}

int cmd_read_resource(CliMcpClient& client, const std::string& uri, bool json_output) {
    Json params = {{"uri", uri}};

    auto result = client.request("resources/read", params);
    if (!result) {
        print_error(result.error().message);
        return 1;
    }

    if (result->contains("error")) {
        print_error((*result)["error"]["message"].get<std::string>());
        return 1;
    }

    auto read_result = ReadResourceResult::from_json((*result)["result"]);

    if (json_output) {
        print_json((*result)["result"]);
    } else {
        for (const auto& content : read_result.contents) {
            if (content.text) {
                std::cout << *content.text << "\n";
            } else if (content.blob) {
                std::cout << color::c(color::dim) << "[Binary data: " 
                          << content.blob->size() << " bytes (base64)]" 
                          << color::c(color::reset) << "\n";
            }
        }
    }
    return 0;
}

int cmd_ping(CliMcpClient& client, bool json_output) {
    auto result = client.request("ping");
    if (!result) {
        print_error(result.error().message);
        return 1;
    }

    if (result->contains("error")) {
        print_error((*result)["error"]["message"].get<std::string>());
        return 1;
    }

    if (json_output) {
        print_json({{"status", "ok"}});
    } else {
        print_success("Server is alive");
    }
    return 0;
}

int cmd_info(CliMcpClient& client, const InitializeResult& init_result, bool json_output) {
    if (json_output) {
        Json output = {
            {"server", {
                {"name", init_result.server_info.name},
                {"version", init_result.server_info.version}
            }},
            {"protocol_version", init_result.protocol_version},
            {"capabilities", {
                {"tools", init_result.capabilities.tools.has_value()},
                {"resources", init_result.capabilities.resources.has_value()},
                {"prompts", init_result.capabilities.prompts.has_value()},
                {"logging", init_result.capabilities.logging.has_value()}
            }}
        };
        if (init_result.instructions) {
            output["instructions"] = *init_result.instructions;
        }
        print_json(output);
    } else {
        print_header("Server Info");
        std::cout << color::c(color::bold) << "Name:     " << color::c(color::reset) 
                  << init_result.server_info.name << "\n";
        std::cout << color::c(color::bold) << "Version:  " << color::c(color::reset) 
                  << init_result.server_info.version << "\n";
        std::cout << color::c(color::bold) << "Protocol: " << color::c(color::reset) 
                  << init_result.protocol_version << "\n";
        
        std::cout << "\n" << color::c(color::bold) << "Capabilities:" << color::c(color::reset) << "\n";
        std::cout << "  • Tools:     " << (init_result.capabilities.tools ? "✓" : "✗") << "\n";
        std::cout << "  • Resources: " << (init_result.capabilities.resources ? "✓" : "✗") << "\n";
        std::cout << "  • Prompts:   " << (init_result.capabilities.prompts ? "✓" : "✗") << "\n";
        std::cout << "  • Logging:   " << (init_result.capabilities.logging ? "✓" : "✗") << "\n";
        
        if (init_result.instructions) {
            std::cout << "\n" << color::c(color::bold) << "Instructions:" << color::c(color::reset) << "\n";
            std::cout << *init_result.instructions << "\n";
        }
    }
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Interactive REPL
// ═══════════════════════════════════════════════════════════════════════════

void print_repl_help() {
    std::cout << "\n" << color::c(color::bold) << "Available commands:" << color::c(color::reset) << "\n";
    std::cout << "  " << color::c(color::yellow) << "tools" << color::c(color::reset) << "              - List available tools\n";
    std::cout << "  " << color::c(color::yellow) << "resources" << color::c(color::reset) << "          - List available resources\n";
    std::cout << "  " << color::c(color::yellow) << "prompts" << color::c(color::reset) << "            - List available prompts\n";
    std::cout << "  " << color::c(color::yellow) << "templates" << color::c(color::reset) << "          - List resource templates\n";
    std::cout << "  " << color::c(color::yellow) << "call <tool> [args]" << color::c(color::reset) << " - Call a tool (args as JSON)\n";
    std::cout << "  " << color::c(color::yellow) << "read <uri>" << color::c(color::reset) << "         - Read a resource\n";
    std::cout << "  " << color::c(color::yellow) << "ping" << color::c(color::reset) << "               - Ping the server\n";
    std::cout << "  " << color::c(color::yellow) << "info" << color::c(color::reset) << "               - Show server info\n";
    std::cout << "  " << color::c(color::yellow) << "help" << color::c(color::reset) << "               - Show this help\n";
    std::cout << "  " << color::c(color::yellow) << "quit" << color::c(color::reset) << "               - Exit\n\n";
}

int run_repl(CliMcpClient& client, const InitializeResult& init_result) {
    std::cout << "\n" << color::c(color::bold) << color::c(color::green)
              << "Connected to " << init_result.server_info.name 
              << " v" << init_result.server_info.version
              << color::c(color::reset) << "\n";
    std::cout << "Type 'help' for available commands, 'quit' to exit.\n";

    std::string line;
    while (true) {
        std::cout << color::c(color::cyan) << "mcpp> " << color::c(color::reset);
        if (!std::getline(std::cin, line)) {
            break;
        }

        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);

        if (line.empty()) continue;

        // Parse command
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            break;
        } else if (cmd == "help" || cmd == "?") {
            print_repl_help();
        } else if (cmd == "tools") {
            cmd_list_tools(client, false);
        } else if (cmd == "resources") {
            cmd_list_resources(client, false);
        } else if (cmd == "prompts") {
            cmd_list_prompts(client, false);
        } else if (cmd == "templates") {
            cmd_list_templates(client, false);
        } else if (cmd == "ping") {
            cmd_ping(client, false);
        } else if (cmd == "info") {
            cmd_info(client, init_result, false);
        } else if (cmd == "call") {
            std::string tool_name;
            iss >> tool_name;
            if (tool_name.empty()) {
                print_error("Usage: call <tool_name> [json_args]");
                continue;
            }
            std::string args;
            std::getline(iss, args);
            // Trim args
            size_t args_start = args.find_first_not_of(" \t");
            if (args_start != std::string::npos) {
                args = args.substr(args_start);
            } else {
                args = "";
            }
            cmd_call_tool(client, tool_name, args, false);
        } else if (cmd == "read") {
            std::string uri;
            iss >> uri;
            if (uri.empty()) {
                print_error("Usage: read <uri>");
                continue;
            }
            cmd_read_resource(client, uri, false);
        } else {
            print_error("Unknown command: " + cmd + ". Type 'help' for available commands.");
        }
    }

    std::cout << "\nGoodbye!\n";
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

// Parse header string "Name: Value" into pair
std::pair<std::string, std::string> parse_header(const std::string& header) {
    auto colon_pos = header.find(':');
    if (colon_pos == std::string::npos) {
        return {header, ""};
    }
    std::string name = header.substr(0, colon_pos);
    std::string value = header.substr(colon_pos + 1);
    // Trim leading whitespace from value
    auto start = value.find_first_not_of(" \t");
    if (start != std::string::npos) {
        value = value.substr(start);
    }
    return {name, value};
}

// Get environment variable with fallback
std::string get_env(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    cxxopts::Options options("mcpp-cli", "MCP Server Testing Tool");
    
    options.add_options()
        // Stdio transport options
        ("c,command", "Server command to execute (stdio transport)", cxxopts::value<std::string>())
        ("a,args", "Arguments for the server command", cxxopts::value<std::vector<std::string>>()->default_value(""))
        
        // HTTP transport options
        ("u,url", "MCP server URL (HTTP transport)", cxxopts::value<std::string>())
        ("H,header", "HTTP header (can be repeated, format: 'Name: Value')", cxxopts::value<std::vector<std::string>>()->default_value(""))
        ("bearer", "Bearer token for Authorization header", cxxopts::value<std::string>())
        
        // Arcade AI shortcuts
        ("arcade", "Arcade gateway slug (shortcut for --url https://api.arcade.dev/mcp/<slug>)", cxxopts::value<std::string>())
        ("arcade-key", "Arcade API key (or use ARCADE_API_KEY env var)", cxxopts::value<std::string>())
        ("arcade-user", "Arcade user ID (or use ARCADE_USER_ID env var)", cxxopts::value<std::string>())
        
        // Commands
        ("list-tools", "List available tools")
        ("list-resources", "List available resources")
        ("list-prompts", "List available prompts")
        ("list-templates", "List resource templates")
        ("call-tool", "Call a tool by name", cxxopts::value<std::string>())
        ("tool-args", "JSON arguments for tool call", cxxopts::value<std::string>()->default_value("{}"))
        ("read-resource", "Read a resource by URI", cxxopts::value<std::string>())
        ("ping", "Ping the server")
        ("info", "Show server info")
        ("i,interactive", "Start interactive REPL mode")
        
        // Output options
        ("j,json", "Output results as JSON")
        ("no-color", "Disable colored output")
        ("content-length", "Use Content-Length framing for stdio (default: raw JSON)")
        ("v,verbose", "Enable verbose logging")
        ("h,help", "Print usage");

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << "\n";
            std::cout << "Examples:\n\n";
            std::cout << color::c(color::bold) << "  Stdio Transport (local servers):\n" << color::c(color::reset);
            std::cout << "    mcpp-cli -c 'npx' -a '-y' -a '@modelcontextprotocol/server-filesystem' -a '/tmp' --list-tools\n";
            std::cout << "    mcpp-cli -c 'python' -a 'server.py' --call-tool read_file --tool-args '{\"path\":\"/tmp/test.txt\"}'\n";
            std::cout << "    mcpp-cli -c 'node' -a 'server.js' --interactive\n\n";
            std::cout << color::c(color::bold) << "  HTTP Transport (remote servers):\n" << color::c(color::reset);
            std::cout << "    mcpp-cli --url 'https://example.com/mcp' --bearer 'secret' --list-tools\n";
            std::cout << "    mcpp-cli -u 'https://api.example.com/mcp' -H 'X-Custom: value' --info\n\n";
            std::cout << color::c(color::bold) << "  Arcade AI Gateway:\n" << color::c(color::reset);
            std::cout << "    mcpp-cli --arcade my-gateway --arcade-key arc_xxx --arcade-user user@example.com --list-tools\n";
            std::cout << "    mcpp-cli --arcade my-gateway --call-tool Github_GetMe\n";
            std::cout << "    # Or with environment variables:\n";
            std::cout << "    export ARCADE_API_KEY=arc_xxx ARCADE_USER_ID=user@example.com\n";
            std::cout << "    mcpp-cli --arcade my-gateway --list-tools\n";
            return 0;
        }

        // Setup
        color::enabled = !result.count("no-color");
        bool json_output = result.count("json") > 0;
        bool use_content_length = result.count("content-length") > 0;

        // Determine transport type and create appropriate transport
        std::unique_ptr<ICliTransport> transport;
        
        bool use_http = result.count("url") > 0 || result.count("arcade") > 0;
        bool use_stdio = result.count("command") > 0;
        
        if (use_http && use_stdio) {
            print_error("Cannot use both --command (stdio) and --url/--arcade (HTTP) at the same time");
            return 1;
        }
        
        if (!use_http && !use_stdio) {
            print_error("Must specify either --command (stdio) or --url/--arcade (HTTP)");
            std::cout << "\n" << options.help() << "\n";
            return 1;
        }
        
        if (use_http) {
            // HTTP Transport
            HttpTransportConfig config;
            
            // Build URL
            if (result.count("arcade")) {
                std::string gateway_slug = result["arcade"].as<std::string>();
                config.base_url = "https://api.arcade.dev/mcp/" + gateway_slug;
                
                // Get Arcade credentials
                std::string api_key = result.count("arcade-key") 
                    ? result["arcade-key"].as<std::string>()
                    : get_env("ARCADE_API_KEY");
                    
                std::string user_id = result.count("arcade-user")
                    ? result["arcade-user"].as<std::string>()
                    : get_env("ARCADE_USER_ID");
                
                if (api_key.empty()) {
                    print_error("Arcade API key required. Use --arcade-key or set ARCADE_API_KEY environment variable");
                    return 1;
                }
                
                if (user_id.empty()) {
                    print_error("Arcade user ID required. Use --arcade-user or set ARCADE_USER_ID environment variable");
                    return 1;
                }
                
                // Set Arcade headers
                config.with_bearer_token(api_key);
                config.with_header("Arcade-User-ID", user_id);
                
                if (!json_output) {
                    std::cout << color::c(color::dim) << "Connecting to Arcade gateway: " 
                              << gateway_slug << color::c(color::reset) << "\n";
                }
            } else {
                config.base_url = result["url"].as<std::string>();
            }
            
            // Add bearer token if provided
            if (result.count("bearer")) {
                config.with_bearer_token(result["bearer"].as<std::string>());
            }
            
            // Add custom headers
            auto headers = result["header"].as<std::vector<std::string>>();
            for (const auto& header : headers) {
                if (!header.empty()) {
                    auto [name, value] = parse_header(header);
                    config.with_header(name, value);
                }
            }
            
            // Disable SSE stream for simple request-response
            config.auto_open_sse_stream = false;
            
            transport = std::make_unique<HttpCliTransport>(config);
            
        } else {
            // Stdio Transport
            ProcessTransportConfig config;
            config.command = result["command"].as<std::string>();
            config.args = result["args"].as<std::vector<std::string>>();
            config.use_content_length_framing = use_content_length;
            config.skip_command_validation = true;  // CLI user controls the command
            
            transport = std::make_unique<StdioCliTransport>(config);
        }

        auto start_result = transport->start();
        if (!start_result) {
            print_error("Failed to start transport: " + start_result.error().message);
            return 1;
        }

        // Give stdio servers a moment to start
        if (use_stdio) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        CliMcpClient client(*transport);

        // Initialize
        auto init_result = client.initialize();
        if (!init_result) {
            print_error("Failed to initialize: " + init_result.error().message);
            transport->stop();
            return 1;
        }

        int exit_code = 0;

        // Execute requested command
        if (result.count("interactive")) {
            exit_code = run_repl(client, *init_result);
        } else if (result.count("list-tools")) {
            exit_code = cmd_list_tools(client, json_output);
        } else if (result.count("list-resources")) {
            exit_code = cmd_list_resources(client, json_output);
        } else if (result.count("list-prompts")) {
            exit_code = cmd_list_prompts(client, json_output);
        } else if (result.count("list-templates")) {
            exit_code = cmd_list_templates(client, json_output);
        } else if (result.count("call-tool")) {
            std::string tool_name = result["call-tool"].as<std::string>();
            std::string tool_args = result["tool-args"].as<std::string>();
            exit_code = cmd_call_tool(client, tool_name, tool_args, json_output);
        } else if (result.count("read-resource")) {
            std::string uri = result["read-resource"].as<std::string>();
            exit_code = cmd_read_resource(client, uri, json_output);
        } else if (result.count("ping")) {
            exit_code = cmd_ping(client, json_output);
        } else if (result.count("info")) {
            exit_code = cmd_info(client, *init_result, json_output);
        } else {
            // Default: show info
            exit_code = cmd_info(client, *init_result, json_output);
        }

        transport->stop();
        return exit_code;

    } catch (const cxxopts::exceptions::exception& e) {
        print_error(e.what());
        return 1;
    }
}

