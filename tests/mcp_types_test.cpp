// ─────────────────────────────────────────────────────────────────────────────
// MCP Types Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for MCP protocol type serialization/deserialization

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/protocol/mcp_types.hpp"

using namespace mcpp;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Implementation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Implementation serialization", "[mcp][types]") {
    Implementation impl{"my-client", "1.0.0"};
    
    auto json = impl.to_json();
    
    REQUIRE(json["name"] == "my-client");
    REQUIRE(json["version"] == "1.0.0");
}

TEST_CASE("Implementation deserialization", "[mcp][types]") {
    Json json = {{"name", "test-server"}, {"version", "2.0.0"}};
    
    auto impl = Implementation::from_json(json);
    
    REQUIRE(impl.name == "test-server");
    REQUIRE(impl.version == "2.0.0");
}

// ═══════════════════════════════════════════════════════════════════════════
// Capabilities Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ClientCapabilities serialization", "[mcp][types]") {
    ClientCapabilities caps;
    caps.roots = ClientCapabilities::Roots{true};
    caps.sampling = ClientCapabilities::Sampling{};
    
    auto json = caps.to_json();
    
    REQUIRE(json["roots"]["listChanged"] == true);
    REQUIRE(json.contains("sampling"));
}

TEST_CASE("ServerCapabilities deserialization", "[mcp][types]") {
    Json json = {
        {"tools", {{"listChanged", true}}},
        {"resources", {{"subscribe", true}, {"listChanged", false}}},
        {"prompts", Json::object()},
        {"logging", Json::object()}
    };
    
    auto caps = ServerCapabilities::from_json(json);
    
    REQUIRE(caps.tools.has_value());
    REQUIRE(caps.tools->list_changed == true);
    REQUIRE(caps.resources.has_value());
    REQUIRE(caps.resources->subscribe == true);
    REQUIRE(caps.prompts.has_value());
    REQUIRE(caps.logging.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Initialize Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("InitializeParams serialization", "[mcp][types]") {
    InitializeParams params;
    params.client_info = {"mcpp", "0.1.0"};
    
    auto json = params.to_json();
    
    REQUIRE(json["protocolVersion"] == MCP_PROTOCOL_VERSION);
    REQUIRE(json["clientInfo"]["name"] == "mcpp");
    REQUIRE(json["clientInfo"]["version"] == "0.1.0");
}

TEST_CASE("InitializeResult deserialization", "[mcp][types]") {
    Json json = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools", Json::object()},
            {"resources", {{"subscribe", true}}}
        }},
        {"serverInfo", {{"name", "test-server"}, {"version", "1.0.0"}}},
        {"instructions", "Welcome to the server!"}
    };
    
    auto result = InitializeResult::from_json(json);
    
    REQUIRE(result.protocol_version == "2024-11-05");
    REQUIRE(result.server_info.name == "test-server");
    REQUIRE(result.capabilities.tools.has_value());
    REQUIRE(result.capabilities.resources.has_value());
    REQUIRE(result.capabilities.resources->subscribe == true);
    REQUIRE(result.instructions.has_value());
    REQUIRE(*result.instructions == "Welcome to the server!");
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Tool deserialization", "[mcp][types][tools]") {
    Json json = {
        {"name", "echo"},
        {"description", "Echoes input"},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"message", {{"type", "string"}}}
            }}
        }}
    };
    
    auto tool = Tool::from_json(json);
    
    REQUIRE(tool.name == "echo");
    REQUIRE(tool.description.has_value());
    REQUIRE(*tool.description == "Echoes input");
    REQUIRE(tool.input_schema["type"] == "object");
}

TEST_CASE("Tool serialization roundtrip", "[mcp][types][tools]") {
    Tool tool;
    tool.name = "add";
    tool.description = "Adds numbers";
    tool.input_schema = {{"type", "object"}};
    
    auto json = tool.to_json();
    auto parsed = Tool::from_json(json);
    
    REQUIRE(parsed.name == tool.name);
    REQUIRE(parsed.description == tool.description);
}

TEST_CASE("ListToolsResult deserialization", "[mcp][types][tools]") {
    Json json = {
        {"tools", Json::array({
            {{"name", "tool1"}},
            {{"name", "tool2"}, {"description", "Second tool"}}
        })},
        {"nextCursor", "cursor123"}
    };
    
    auto result = ListToolsResult::from_json(json);
    
    REQUIRE(result.tools.size() == 2);
    REQUIRE(result.tools[0].name == "tool1");
    REQUIRE(result.tools[1].name == "tool2");
    REQUIRE(result.next_cursor.has_value());
    REQUIRE(*result.next_cursor == "cursor123");
}

TEST_CASE("CallToolParams serialization", "[mcp][types][tools]") {
    CallToolParams params;
    params.name = "echo";
    params.arguments = {{"message", "hello"}};
    
    auto json = params.to_json();
    
    REQUIRE(json["name"] == "echo");
    REQUIRE(json["arguments"]["message"] == "hello");
}

TEST_CASE("CallToolResult deserialization", "[mcp][types][tools]") {
    Json json = {
        {"content", Json::array({
            {{"type", "text"}, {"text", "Hello, world!"}},
            {{"type", "image"}, {"data", "base64..."}, {"mimeType", "image/png"}}
        })},
        {"isError", false}
    };
    
    auto result = CallToolResult::from_json(json);
    
    REQUIRE(result.content.size() == 2);
    REQUIRE(result.is_error == false);
    
    // Check first content is TextContent
    REQUIRE(std::holds_alternative<TextContent>(result.content[0]));
    REQUIRE(std::get<TextContent>(result.content[0]).text == "Hello, world!");
    
    // Check second content is ImageContent
    REQUIRE(std::holds_alternative<ImageContent>(result.content[1]));
    REQUIRE(std::get<ImageContent>(result.content[1]).mime_type == "image/png");
}

// ═══════════════════════════════════════════════════════════════════════════
// Resource Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Resource deserialization", "[mcp][types][resources]") {
    Json json = {
        {"uri", "file:///config.json"},
        {"name", "Configuration"},
        {"description", "App configuration"},
        {"mimeType", "application/json"}
    };
    
    auto resource = Resource::from_json(json);
    
    REQUIRE(resource.uri == "file:///config.json");
    REQUIRE(resource.name == "Configuration");
    REQUIRE(resource.description.has_value());
    REQUIRE(resource.mime_type.has_value());
    REQUIRE(*resource.mime_type == "application/json");
}

TEST_CASE("ListResourcesResult deserialization", "[mcp][types][resources]") {
    Json json = {
        {"resources", Json::array({
            {{"uri", "file:///a"}, {"name", "A"}},
            {{"uri", "file:///b"}, {"name", "B"}}
        })}
    };
    
    auto result = ListResourcesResult::from_json(json);
    
    REQUIRE(result.resources.size() == 2);
    REQUIRE(result.resources[0].uri == "file:///a");
    REQUIRE(result.next_cursor.has_value() == false);
}

TEST_CASE("ReadResourceResult deserialization", "[mcp][types][resources]") {
    Json json = {
        {"contents", Json::array({
            {
                {"uri", "file:///config.json"},
                {"mimeType", "application/json"},
                {"text", R"({"key": "value"})"}
            }
        })}
    };
    
    auto result = ReadResourceResult::from_json(json);
    
    REQUIRE(result.contents.size() == 1);
    REQUIRE(result.contents[0].uri == "file:///config.json");
    REQUIRE(result.contents[0].text.has_value());
    REQUIRE(*result.contents[0].text == R"({"key": "value"})");
}

// ═══════════════════════════════════════════════════════════════════════════
// Prompt Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prompt deserialization", "[mcp][types][prompts]") {
    Json json = {
        {"name", "code-review"},
        {"description", "Review code for issues"},
        {"arguments", Json::array({
            {{"name", "language"}, {"required", true}},
            {{"name", "style"}, {"description", "Review style"}}
        })}
    };
    
    auto prompt = Prompt::from_json(json);
    
    REQUIRE(prompt.name == "code-review");
    REQUIRE(prompt.description.has_value());
    REQUIRE(prompt.arguments.size() == 2);
    REQUIRE(prompt.arguments[0].name == "language");
    REQUIRE(prompt.arguments[0].required == true);
    REQUIRE(prompt.arguments[1].required == false);
}

TEST_CASE("ListPromptsResult deserialization", "[mcp][types][prompts]") {
    Json json = {
        {"prompts", Json::array({
            {{"name", "prompt1"}},
            {{"name", "prompt2"}}
        })}
    };
    
    auto result = ListPromptsResult::from_json(json);
    
    REQUIRE(result.prompts.size() == 2);
}

TEST_CASE("GetPromptResult deserialization", "[mcp][types][prompts]") {
    Json json = {
        {"description", "A helpful prompt"},
        {"messages", Json::array({
            {
                {"role", "user"},
                {"content", {{"type", "text"}, {"text", "Hello"}}}
            },
            {
                {"role", "assistant"},
                {"content", {{"type", "text"}, {"text", "Hi there!"}}}
            }
        })}
    };
    
    auto result = GetPromptResult::from_json(json);
    
    REQUIRE(result.description.has_value());
    REQUIRE(result.messages.size() == 2);
    REQUIRE(result.messages[0].role == "user");
    REQUIRE(result.messages[1].role == "assistant");
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("McpError deserialization", "[mcp][types][error]") {
    Json json = {
        {"code", -32601},
        {"message", "Method not found"},
        {"data", {{"method", "unknown/method"}}}
    };
    
    auto error = McpError::from_json(json);
    
    REQUIRE(error.code == ErrorCode::MethodNotFound);
    REQUIRE(error.message == "Method not found");
    REQUIRE(error.data.has_value());
    REQUIRE((*error.data)["method"] == "unknown/method");
}

TEST_CASE("McpError serialization", "[mcp][types][error]") {
    McpError error{ErrorCode::InvalidParams, "Missing required field"};
    
    auto json = error.to_json();
    
    REQUIRE(json["code"] == -32602);
    REQUIRE(json["message"] == "Missing required field");
}

// ═══════════════════════════════════════════════════════════════════════════
// Logging Level Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("LoggingLevel conversion", "[mcp][types][logging]") {
    REQUIRE(to_string(LoggingLevel::Debug) == "debug");
    REQUIRE(to_string(LoggingLevel::Error) == "error");
    REQUIRE(to_string(LoggingLevel::Emergency) == "emergency");
    
    REQUIRE(logging_level_from_string("debug") == LoggingLevel::Debug);
    REQUIRE(logging_level_from_string("error") == LoggingLevel::Error);
    REQUIRE(logging_level_from_string("unknown") == LoggingLevel::Info);  // Default
}

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation Types Tests
// ═══════════════════════════════════════════════════════════════════════════
//
// Elicitation Flow:
// ┌────────┐                    ┌────────┐                    ┌────────┐
// │ Server │───elicitation/────▶│ Client │───show form/url───▶│  User  │
// │        │    create          │        │◀──user response────│        │
// │        │◀───result──────────│        │                    │        │
// └────────┘                    └────────┘                    └────────┘

TEST_CASE("ElicitationMode string conversion", "[mcp][types][elicitation]") {
    REQUIRE(to_string(ElicitationMode::Form) == "form");
    REQUIRE(to_string(ElicitationMode::Url) == "url");
    
    REQUIRE(elicitation_mode_from_string("form") == ElicitationMode::Form);
    REQUIRE(elicitation_mode_from_string("url") == ElicitationMode::Url);
    REQUIRE(elicitation_mode_from_string("invalid") == ElicitationMode::Form);  // Default
}

TEST_CASE("ElicitationAction string conversion", "[mcp][types][elicitation]") {
    REQUIRE(to_string(ElicitationAction::Accept) == "accept");
    REQUIRE(to_string(ElicitationAction::Decline) == "decline");
    REQUIRE(to_string(ElicitationAction::Dismiss) == "dismiss");
    REQUIRE(to_string(ElicitationAction::Opened) == "opened");
    
    REQUIRE(elicitation_action_from_string("accept") == ElicitationAction::Accept);
    REQUIRE(elicitation_action_from_string("decline") == ElicitationAction::Decline);
    REQUIRE(elicitation_action_from_string("dismiss") == ElicitationAction::Dismiss);
    REQUIRE(elicitation_action_from_string("opened") == ElicitationAction::Opened);
    REQUIRE(elicitation_action_from_string("invalid") == ElicitationAction::Dismiss);  // Default
}

TEST_CASE("ElicitationCapability serialization", "[mcp][types][elicitation]") {
    SECTION("form only (default)") {
        ElicitationCapability cap;
        auto json = cap.to_json();
        
        REQUIRE(json.contains("form"));
        REQUIRE(json["form"].is_object());
        REQUIRE_FALSE(json.contains("url"));
    }
    
    SECTION("form and url") {
        ElicitationCapability cap{true, true};
        auto json = cap.to_json();
        
        REQUIRE(json.contains("form"));
        REQUIRE(json.contains("url"));
    }
    
    SECTION("empty capability (backwards compat)") {
        // Per spec: empty {} is equivalent to form-only
        Json json = Json::object();
        auto cap = ElicitationCapability::from_json(json);
        
        REQUIRE(cap.form == true);
        REQUIRE(cap.url == false);
    }
}

TEST_CASE("ElicitationCapability deserialization", "[mcp][types][elicitation]") {
    SECTION("form and url enabled") {
        Json json = {{"form", Json::object()}, {"url", Json::object()}};
        auto cap = ElicitationCapability::from_json(json);
        
        REQUIRE(cap.form == true);
        REQUIRE(cap.url == true);
    }
    
    SECTION("url only") {
        Json json = {{"url", Json::object()}};
        auto cap = ElicitationCapability::from_json(json);
        
        REQUIRE(cap.form == false);
        REQUIRE(cap.url == true);
    }
}

TEST_CASE("FormElicitationParams serialization", "[mcp][types][elicitation]") {
    FormElicitationParams params;
    params.message = "Please enter your username";
    params.requested_schema = {
        {"type", "object"},
        {"properties", {
            {"username", {{"type", "string"}}}
        }},
        {"required", {"username"}}
    };
    
    auto json = params.to_json();
    
    REQUIRE(json["mode"] == "form");
    REQUIRE(json["message"] == "Please enter your username");
    REQUIRE(json["requestedSchema"]["type"] == "object");
    REQUIRE(json["requestedSchema"]["properties"]["username"]["type"] == "string");
}

TEST_CASE("FormElicitationParams deserialization", "[mcp][types][elicitation]") {
    Json json = {
        {"mode", "form"},
        {"message", "Enter API key"},
        {"requestedSchema", {
            {"type", "object"},
            {"properties", {{"key", {{"type", "string"}}}}}
        }}
    };
    
    auto params = FormElicitationParams::from_json(json);
    
    REQUIRE(params.message == "Enter API key");
    REQUIRE(params.requested_schema["type"] == "object");
}

TEST_CASE("UrlElicitationParams serialization", "[mcp][types][elicitation]") {
    UrlElicitationParams params;
    params.elicitation_id = "abc-123";
    params.url = "https://github.com/login/oauth/authorize?client_id=xxx";
    params.message = "Please authorize GitHub access";
    
    auto json = params.to_json();
    
    REQUIRE(json["mode"] == "url");
    REQUIRE(json["elicitationId"] == "abc-123");
    REQUIRE(json["url"] == "https://github.com/login/oauth/authorize?client_id=xxx");
    REQUIRE(json["message"] == "Please authorize GitHub access");
}

TEST_CASE("UrlElicitationParams deserialization", "[mcp][types][elicitation]") {
    Json json = {
        {"mode", "url"},
        {"elicitationId", "xyz-789"},
        {"url", "https://example.com/auth"},
        {"message", "Authenticate please"}
    };
    
    auto params = UrlElicitationParams::from_json(json);
    
    REQUIRE(params.elicitation_id == "xyz-789");
    REQUIRE(params.url == "https://example.com/auth");
    REQUIRE(params.message == "Authenticate please");
}

TEST_CASE("ElicitationResult serialization", "[mcp][types][elicitation]") {
    SECTION("accept with content") {
        ElicitationResult result;
        result.action = ElicitationAction::Accept;
        result.content = Json{{"username", "octocat"}};
        
        auto json = result.to_json();
        
        REQUIRE(json["action"] == "accept");
        REQUIRE(json["content"]["username"] == "octocat");
    }
    
    SECTION("decline without content") {
        ElicitationResult result;
        result.action = ElicitationAction::Decline;
        
        auto json = result.to_json();
        
        REQUIRE(json["action"] == "decline");
        REQUIRE_FALSE(json.contains("content"));
    }
    
    SECTION("opened (url mode)") {
        ElicitationResult result;
        result.action = ElicitationAction::Opened;
        
        auto json = result.to_json();
        
        REQUIRE(json["action"] == "opened");
    }
}

TEST_CASE("ElicitationResult deserialization", "[mcp][types][elicitation]") {
    SECTION("accept with content") {
        Json json = {
            {"action", "accept"},
            {"content", {{"apiKey", "sk-12345"}}}
        };
        
        auto result = ElicitationResult::from_json(json);
        
        REQUIRE(result.action == ElicitationAction::Accept);
        REQUIRE(result.content.has_value());
        REQUIRE((*result.content)["apiKey"] == "sk-12345");
    }
    
    SECTION("dismiss") {
        Json json = {{"action", "dismiss"}};
        
        auto result = ElicitationResult::from_json(json);
        
        REQUIRE(result.action == ElicitationAction::Dismiss);
        REQUIRE_FALSE(result.content.has_value());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "mcpp/client/elicitation_handler.hpp"

TEST_CASE("NullElicitationHandler returns Dismiss", "[mcp][types][elicitation][handler]") {
    NullElicitationHandler handler;
    
    SECTION("form mode") {
        auto result = handler.handle_form("Enter data", Json::object());
        
        REQUIRE(result.action == ElicitationAction::Dismiss);
        REQUIRE_FALSE(result.content.has_value());
    }
    
    SECTION("url mode") {
        auto result = handler.handle_url("id-123", "https://example.com", "Open link");
        
        REQUIRE(result.action == ElicitationAction::Dismiss);
        REQUIRE_FALSE(result.content.has_value());
    }
}

// Example: Custom handler implementation for testing
namespace {
class TestElicitationHandler : public IElicitationHandler {
public:
    std::string last_form_message;
    Json last_form_schema;
    std::string last_url_id;
    std::string last_url;
    std::string last_url_message;
    
    ElicitationResult form_response{ElicitationAction::Accept, Json{{"test", "value"}}};
    ElicitationResult url_response{ElicitationAction::Opened, std::nullopt};
    
    ElicitationResult handle_form(
        const std::string& message,
        const Json& schema
    ) override {
        last_form_message = message;
        last_form_schema = schema;
        return form_response;
    }
    
    ElicitationResult handle_url(
        const std::string& elicitation_id,
        const std::string& url,
        const std::string& message
    ) override {
        last_url_id = elicitation_id;
        last_url = url;
        last_url_message = message;
        return url_response;
    }
};
}  // namespace

TEST_CASE("Custom IElicitationHandler implementation", "[mcp][types][elicitation][handler]") {
    TestElicitationHandler handler;
    
    SECTION("form handler receives correct parameters") {
        Json schema = {{"type", "object"}};
        auto result = handler.handle_form("Please enter name", schema);
        
        REQUIRE(handler.last_form_message == "Please enter name");
        REQUIRE(handler.last_form_schema == schema);
        REQUIRE(result.action == ElicitationAction::Accept);
        REQUIRE(result.content.has_value());
    }
    
    SECTION("url handler receives correct parameters") {
        auto result = handler.handle_url("abc-123", "https://auth.example.com", "Authorize");
        
        REQUIRE(handler.last_url_id == "abc-123");
        REQUIRE(handler.last_url == "https://auth.example.com");
        REQUIRE(handler.last_url_message == "Authorize");
        REQUIRE(result.action == ElicitationAction::Opened);
    }
    
    SECTION("handler can return decline") {
        handler.form_response = {ElicitationAction::Decline, std::nullopt};
        auto result = handler.handle_form("Enter secret", Json::object());
        
        REQUIRE(result.action == ElicitationAction::Decline);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Sampling Types Tests
// ═══════════════════════════════════════════════════════════════════════════
//
// Sampling Flow (Human-in-the-Loop):
// ┌────────┐         ┌────────┐         ┌────────┐         ┌────────┐
// │ Server │────────▶│ Client │────────▶│  User  │────────▶│  LLM   │
// │        │ create  │        │ review? │        │ approve │        │
// │        │ Message │        │         │        │         │        │
// │        │◀────────│        │◀────────│        │◀────────│        │
// └────────┘ result  └────────┘         └────────┘         └────────┘

TEST_CASE("SamplingRole string conversion", "[mcp][types][sampling]") {
    REQUIRE(to_string(SamplingRole::User) == "user");
    REQUIRE(to_string(SamplingRole::Assistant) == "assistant");
    
    REQUIRE(sampling_role_from_string("user") == SamplingRole::User);
    REQUIRE(sampling_role_from_string("assistant") == SamplingRole::Assistant);
    REQUIRE(sampling_role_from_string("invalid") == SamplingRole::User);  // Default
}

TEST_CASE("StopReason string conversion", "[mcp][types][sampling]") {
    REQUIRE(to_string(StopReason::EndTurn) == "endTurn");
    REQUIRE(to_string(StopReason::StopSequence) == "stopSequence");
    REQUIRE(to_string(StopReason::MaxTokens) == "maxTokens");
    
    REQUIRE(stop_reason_from_string("endTurn") == StopReason::EndTurn);
    REQUIRE(stop_reason_from_string("stopSequence") == StopReason::StopSequence);
    REQUIRE(stop_reason_from_string("maxTokens") == StopReason::MaxTokens);
    REQUIRE(stop_reason_from_string("invalid") == StopReason::EndTurn);  // Default
}

TEST_CASE("IncludeContext string conversion", "[mcp][types][sampling]") {
    REQUIRE(to_string(IncludeContext::None) == "none");
    REQUIRE(to_string(IncludeContext::ThisServer) == "thisServer");
    REQUIRE(to_string(IncludeContext::AllServers) == "allServers");
    
    REQUIRE(include_context_from_string("none") == IncludeContext::None);
    REQUIRE(include_context_from_string("thisServer") == IncludeContext::ThisServer);
    REQUIRE(include_context_from_string("allServers") == IncludeContext::AllServers);
    REQUIRE(include_context_from_string("invalid") == IncludeContext::None);  // Default
}

TEST_CASE("SamplingMessage serialization", "[mcp][types][sampling]") {
    SECTION("text content") {
        SamplingMessage msg;
        msg.role = SamplingRole::User;
        msg.content = TextContent{"Hello, please summarize this.", std::nullopt};
        
        auto json = msg.to_json();
        
        REQUIRE(json["role"] == "user");
        REQUIRE(json["content"]["type"] == "text");
        REQUIRE(json["content"]["text"] == "Hello, please summarize this.");
    }
    
    SECTION("image content") {
        SamplingMessage msg;
        msg.role = SamplingRole::User;
        msg.content = ImageContent{"base64data==", "image/png", std::nullopt};
        
        auto json = msg.to_json();
        
        REQUIRE(json["role"] == "user");
        REQUIRE(json["content"]["type"] == "image");
        REQUIRE(json["content"]["data"] == "base64data==");
        REQUIRE(json["content"]["mimeType"] == "image/png");
    }
    
    SECTION("assistant response") {
        SamplingMessage msg;
        msg.role = SamplingRole::Assistant;
        msg.content = TextContent{"Here is the summary...", std::nullopt};
        
        auto json = msg.to_json();
        
        REQUIRE(json["role"] == "assistant");
        REQUIRE(json["content"]["type"] == "text");
    }
}

TEST_CASE("SamplingMessage deserialization", "[mcp][types][sampling]") {
    SECTION("text content") {
        Json json = {
            {"role", "user"},
            {"content", {
                {"type", "text"},
                {"text", "Analyze this code"}
            }}
        };
        
        auto msg = SamplingMessage::from_json(json);
        
        REQUIRE(msg.role == SamplingRole::User);
        REQUIRE(std::holds_alternative<TextContent>(msg.content));
        REQUIRE(std::get<TextContent>(msg.content).text == "Analyze this code");
    }
    
    SECTION("image content") {
        Json json = {
            {"role", "user"},
            {"content", {
                {"type", "image"},
                {"data", "iVBORw0KGgo="},
                {"mimeType", "image/png"}
            }}
        };
        
        auto msg = SamplingMessage::from_json(json);
        
        REQUIRE(std::holds_alternative<ImageContent>(msg.content));
        auto& img = std::get<ImageContent>(msg.content);
        REQUIRE(img.data == "iVBORw0KGgo=");
        REQUIRE(img.mime_type == "image/png");
    }
}

TEST_CASE("ModelPreferences serialization", "[mcp][types][sampling]") {
    ModelPreferences prefs;
    prefs.hints = {ModelHint{"claude-3-5-sonnet"}, ModelHint{"gpt-4"}};
    prefs.cost_priority = 0.3;
    prefs.speed_priority = 0.8;
    prefs.intelligence_priority = 0.9;
    
    auto json = prefs.to_json();
    
    REQUIRE(json["hints"].size() == 2);
    REQUIRE(json["hints"][0]["name"] == "claude-3-5-sonnet");
    REQUIRE(json["hints"][1]["name"] == "gpt-4");
    REQUIRE(json["costPriority"].get<double>() == Catch::Approx(0.3));
    REQUIRE(json["speedPriority"].get<double>() == Catch::Approx(0.8));
    REQUIRE(json["intelligencePriority"].get<double>() == Catch::Approx(0.9));
}

TEST_CASE("ModelPreferences deserialization", "[mcp][types][sampling]") {
    Json json = {
        {"hints", {{{"name", "claude-3-opus"}}}},
        {"costPriority", 0.5},
        {"speedPriority", 0.7}
    };
    
    auto prefs = ModelPreferences::from_json(json);
    
    REQUIRE(prefs.hints.size() == 1);
    REQUIRE(prefs.hints[0].name == "claude-3-opus");
    REQUIRE(prefs.cost_priority.has_value());
    REQUIRE(*prefs.cost_priority == Catch::Approx(0.5));
    REQUIRE(prefs.speed_priority.has_value());
    REQUIRE_FALSE(prefs.intelligence_priority.has_value());
}

TEST_CASE("CreateMessageParams serialization", "[mcp][types][sampling]") {
    CreateMessageParams params;
    params.messages = {
        SamplingMessage{SamplingRole::User, TextContent{"Summarize this", std::nullopt}}
    };
    params.system_prompt = "You are a helpful assistant.";
    params.include_context = IncludeContext::ThisServer;
    params.max_tokens = 500;
    params.stop_sequences = {"END", "STOP"};
    
    auto json = params.to_json();
    
    REQUIRE(json["messages"].size() == 1);
    REQUIRE(json["messages"][0]["role"] == "user");
    REQUIRE(json["systemPrompt"] == "You are a helpful assistant.");
    REQUIRE(json["includeContext"] == "thisServer");
    REQUIRE(json["maxTokens"] == 500);
    REQUIRE(json["stopSequences"].size() == 2);
}

TEST_CASE("CreateMessageParams deserialization", "[mcp][types][sampling]") {
    Json json = {
        {"messages", {
            {{"role", "user"}, {"content", {{"type", "text"}, {"text", "Hello"}}}}
        }},
        {"modelPreferences", {
            {"hints", {{{"name", "claude"}}}},
            {"speedPriority", 0.9}
        }},
        {"systemPrompt", "Be concise."},
        {"includeContext", "allServers"},
        {"maxTokens", 1000}
    };
    
    auto params = CreateMessageParams::from_json(json);
    
    REQUIRE(params.messages.size() == 1);
    REQUIRE(params.model_preferences.has_value());
    REQUIRE(params.model_preferences->hints.size() == 1);
    REQUIRE(params.system_prompt == "Be concise.");
    REQUIRE(params.include_context == IncludeContext::AllServers);
    REQUIRE(params.max_tokens == 1000);
}

TEST_CASE("CreateMessageResult serialization", "[mcp][types][sampling]") {
    CreateMessageResult result;
    result.role = SamplingRole::Assistant;
    result.content = TextContent{"Here is your summary...", std::nullopt};
    result.model = "claude-3-5-sonnet-20241022";
    result.stop_reason = StopReason::EndTurn;
    
    auto json = result.to_json();
    
    REQUIRE(json["role"] == "assistant");
    REQUIRE(json["content"]["type"] == "text");
    REQUIRE(json["content"]["text"] == "Here is your summary...");
    REQUIRE(json["model"] == "claude-3-5-sonnet-20241022");
    REQUIRE(json["stopReason"] == "endTurn");
}

TEST_CASE("CreateMessageResult deserialization", "[mcp][types][sampling]") {
    Json json = {
        {"role", "assistant"},
        {"content", {{"type", "text"}, {"text", "Response text"}}},
        {"model", "gpt-4-turbo"},
        {"stopReason", "maxTokens"}
    };
    
    auto result = CreateMessageResult::from_json(json);
    
    REQUIRE(result.role == SamplingRole::Assistant);
    REQUIRE(std::holds_alternative<TextContent>(result.content));
    REQUIRE(std::get<TextContent>(result.content).text == "Response text");
    REQUIRE(result.model == "gpt-4-turbo");
    REQUIRE(result.stop_reason == StopReason::MaxTokens);
}

// ═══════════════════════════════════════════════════════════════════════════
// Sampling Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "mcpp/client/sampling_handler.hpp"

TEST_CASE("NullSamplingHandler returns nullopt", "[mcp][types][sampling][handler]") {
    NullSamplingHandler handler;
    
    CreateMessageParams params;
    params.messages = {SamplingMessage{SamplingRole::User, TextContent{"Test", std::nullopt}}};
    
    auto result = handler.handle_create_message(params);
    
    REQUIRE_FALSE(result.has_value());
}

// Example: Custom sampling handler for testing
namespace {
class TestSamplingHandler : public ISamplingHandler {
public:
    CreateMessageParams last_params;
    std::optional<CreateMessageResult> response;
    
    std::optional<CreateMessageResult> handle_create_message(
        const CreateMessageParams& params
    ) override {
        last_params = params;
        return response;
    }
};
}  // namespace

TEST_CASE("Custom ISamplingHandler implementation", "[mcp][types][sampling][handler]") {
    TestSamplingHandler handler;
    
    SECTION("handler receives correct parameters") {
        CreateMessageParams params;
        params.messages = {SamplingMessage{SamplingRole::User, TextContent{"Summarize", std::nullopt}}};
        params.max_tokens = 100;
        
        handler.handle_create_message(params);
        
        REQUIRE(handler.last_params.messages.size() == 1);
        REQUIRE(handler.last_params.max_tokens == 100);
    }
    
    SECTION("handler can return a response") {
        handler.response = CreateMessageResult{
            SamplingRole::Assistant,
            TextContent{"Summary here", std::nullopt},
            "test-model",
            StopReason::EndTurn
        };
        
        CreateMessageParams params;
        params.messages = {SamplingMessage{SamplingRole::User, TextContent{"Test", std::nullopt}}};
        
        auto result = handler.handle_create_message(params);
        
        REQUIRE(result.has_value());
        REQUIRE(result->model == "test-model");
        REQUIRE(std::get<TextContent>(result->content).text == "Summary here");
    }
    
    SECTION("handler can decline by returning nullopt") {
        handler.response = std::nullopt;
        
        CreateMessageParams params;
        params.messages = {SamplingMessage{SamplingRole::User, TextContent{"Test", std::nullopt}}};
        
        auto result = handler.handle_create_message(params);
        
        REQUIRE_FALSE(result.has_value());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Roots Types Tests
// ═══════════════════════════════════════════════════════════════════════════
//
// Roots define filesystem boundaries exposed to servers:
// ┌────────┐                    ┌────────┐
// │ Server │───roots/list──────▶│ Client │
// │        │◀──list of roots────│        │
// │        │                    │        │
// │        │◀─roots_changed─────│        │  (notification when roots change)
// └────────┘                    └────────┘

TEST_CASE("Root serialization", "[mcp][types][roots]") {
    SECTION("with name") {
        Root root;
        root.uri = "file:///home/user/projects/myapp";
        root.name = "My Application";
        
        auto json = root.to_json();
        
        REQUIRE(json["uri"] == "file:///home/user/projects/myapp");
        REQUIRE(json["name"] == "My Application");
    }
    
    SECTION("without name") {
        Root root;
        root.uri = "file:///tmp/workspace";
        
        auto json = root.to_json();
        
        REQUIRE(json["uri"] == "file:///tmp/workspace");
        REQUIRE_FALSE(json.contains("name"));
    }
}

TEST_CASE("Root deserialization", "[mcp][types][roots]") {
    SECTION("with name") {
        Json json = {
            {"uri", "file:///var/data"},
            {"name", "Data Directory"}
        };
        
        auto root = Root::from_json(json);
        
        REQUIRE(root.uri == "file:///var/data");
        REQUIRE(root.name.has_value());
        REQUIRE(*root.name == "Data Directory");
    }
    
    SECTION("without name") {
        Json json = {{"uri", "file:///opt/libs"}};
        
        auto root = Root::from_json(json);
        
        REQUIRE(root.uri == "file:///opt/libs");
        REQUIRE_FALSE(root.name.has_value());
    }
}

TEST_CASE("ListRootsResult serialization", "[mcp][types][roots]") {
    ListRootsResult result;
    result.roots = {
        Root{"file:///home/user/project", "Main Project"},
        Root{"file:///home/user/libs", std::nullopt}
    };
    
    auto json = result.to_json();
    
    REQUIRE(json["roots"].size() == 2);
    REQUIRE(json["roots"][0]["uri"] == "file:///home/user/project");
    REQUIRE(json["roots"][0]["name"] == "Main Project");
    REQUIRE(json["roots"][1]["uri"] == "file:///home/user/libs");
    REQUIRE_FALSE(json["roots"][1].contains("name"));
}

TEST_CASE("ListRootsResult deserialization", "[mcp][types][roots]") {
    Json json = {
        {"roots", {
            {{"uri", "file:///workspace/a"}, {"name", "Project A"}},
            {{"uri", "file:///workspace/b"}}
        }}
    };
    
    auto result = ListRootsResult::from_json(json);
    
    REQUIRE(result.roots.size() == 2);
    REQUIRE(result.roots[0].uri == "file:///workspace/a");
    REQUIRE(result.roots[0].name == "Project A");
    REQUIRE(result.roots[1].uri == "file:///workspace/b");
    REQUIRE_FALSE(result.roots[1].name.has_value());
}

TEST_CASE("RootsCapability serialization", "[mcp][types][roots]") {
    SECTION("with list_changed") {
        RootsCapability cap{true};
        auto json = cap.to_json();
        
        REQUIRE(json["listChanged"] == true);
    }
    
    SECTION("without list_changed") {
        RootsCapability cap{false};
        auto json = cap.to_json();
        
        REQUIRE(json["listChanged"] == false);
    }
}

TEST_CASE("RootsCapability deserialization", "[mcp][types][roots]") {
    SECTION("listChanged true") {
        Json json = {{"listChanged", true}};
        auto cap = RootsCapability::from_json(json);
        
        REQUIRE(cap.list_changed == true);
    }
    
    SECTION("listChanged false") {
        Json json = {{"listChanged", false}};
        auto cap = RootsCapability::from_json(json);
        
        REQUIRE(cap.list_changed == false);
    }
    
    SECTION("empty object defaults to false") {
        Json json = Json::object();
        auto cap = RootsCapability::from_json(json);
        
        REQUIRE(cap.list_changed == false);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Roots Handler Tests
// ═══════════════════════════════════════════════════════════════════════════

#include "mcpp/client/roots_handler.hpp"

TEST_CASE("StaticRootsHandler returns configured roots", "[mcp][types][roots][handler]") {
    std::vector<Root> roots = {
        Root{"file:///home/user/project", "My Project"},
        Root{"file:///shared/libs", std::nullopt}
    };
    
    StaticRootsHandler handler(roots);
    
    auto result = handler.list_roots();
    
    REQUIRE(result.roots.size() == 2);
    REQUIRE(result.roots[0].uri == "file:///home/user/project");
    REQUIRE(result.roots[0].name == "My Project");
    REQUIRE(result.roots[1].uri == "file:///shared/libs");
}

TEST_CASE("StaticRootsHandler with empty roots", "[mcp][types][roots][handler]") {
    StaticRootsHandler handler({});
    
    auto result = handler.list_roots();
    
    REQUIRE(result.roots.empty());
}

// Example: Custom roots handler for testing
namespace {
class TestRootsHandler : public IRootsHandler {
public:
    std::vector<Root> roots;
    int list_roots_called{0};
    
    ListRootsResult list_roots() override {
        ++list_roots_called;
        return ListRootsResult{roots};
    }
};
}  // namespace

TEST_CASE("Custom IRootsHandler implementation", "[mcp][types][roots][handler]") {
    TestRootsHandler handler;
    handler.roots = {
        Root{"file:///test/path", "Test"}
    };
    
    SECTION("list_roots returns configured roots") {
        auto result = handler.list_roots();
        
        REQUIRE(result.roots.size() == 1);
        REQUIRE(result.roots[0].uri == "file:///test/path");
        REQUIRE(handler.list_roots_called == 1);
    }
    
    SECTION("list_roots can be called multiple times") {
        handler.list_roots();
        handler.list_roots();
        handler.list_roots();
        
        REQUIRE(handler.list_roots_called == 3);
    }
    
    SECTION("roots can be updated dynamically") {
        auto result1 = handler.list_roots();
        REQUIRE(result1.roots.size() == 1);
        
        handler.roots.push_back(Root{"file:///new/root", "New"});
        
        auto result2 = handler.list_roots();
        REQUIRE(result2.roots.size() == 2);
    }
}

TEST_CASE("MutableRootsHandler dynamic updates", "[mcp][types][roots][handler]") {
    MutableRootsHandler handler;
    
    SECTION("starts empty by default") {
        auto result = handler.list_roots();
        REQUIRE(result.roots.empty());
        REQUIRE(handler.root_count() == 0);
    }
    
    SECTION("can be initialized with roots") {
        MutableRootsHandler h({
            Root{"file:///init/path", "Initial"}
        });
        
        auto result = h.list_roots();
        REQUIRE(result.roots.size() == 1);
    }
    
    SECTION("add_root appends to list") {
        handler.add_root(Root{"file:///first", "First"});
        handler.add_root(Root{"file:///second", "Second"});
        
        auto result = handler.list_roots();
        REQUIRE(result.roots.size() == 2);
        REQUIRE(result.roots[0].uri == "file:///first");
        REQUIRE(result.roots[1].uri == "file:///second");
    }
    
    SECTION("set_roots replaces all roots") {
        handler.add_root(Root{"file:///old", "Old"});
        
        handler.set_roots({
            Root{"file:///new1", "New1"},
            Root{"file:///new2", "New2"}
        });
        
        auto result = handler.list_roots();
        REQUIRE(result.roots.size() == 2);
        REQUIRE(result.roots[0].uri == "file:///new1");
    }
    
    SECTION("clear_roots removes all roots") {
        handler.add_root(Root{"file:///test", "Test"});
        REQUIRE(handler.root_count() == 1);
        
        handler.clear_roots();
        
        REQUIRE(handler.root_count() == 0);
        auto result = handler.list_roots();
        REQUIRE(result.roots.empty());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Resource Templates Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ResourceTemplate serialization", "[mcp][types][resource-template]") {
    ResourceTemplate tmpl;
    tmpl.uri_template = "file:///{path}";
    tmpl.name = "File Access";
    tmpl.description = "Access files by path";
    tmpl.mime_type = "text/plain";
    
    auto json = tmpl.to_json();
    
    REQUIRE(json["uriTemplate"] == "file:///{path}");
    REQUIRE(json["name"] == "File Access");
    REQUIRE(json["description"] == "Access files by path");
    REQUIRE(json["mimeType"] == "text/plain");
}

TEST_CASE("ResourceTemplate deserialization", "[mcp][types][resource-template]") {
    Json json = {
        {"uriTemplate", "http://api.example.com/{endpoint}"},
        {"name", "API Endpoint"},
        {"description", "Access API endpoints"},
        {"mimeType", "application/json"}
    };
    
    auto tmpl = ResourceTemplate::from_json(json);
    
    REQUIRE(tmpl.uri_template == "http://api.example.com/{endpoint}");
    REQUIRE(tmpl.name == "API Endpoint");
    REQUIRE(tmpl.description.value() == "Access API endpoints");
    REQUIRE(tmpl.mime_type.value() == "application/json");
}

TEST_CASE("ResourceTemplate minimal deserialization", "[mcp][types][resource-template]") {
    Json json = {
        {"uriTemplate", "db:///{table}"},
        {"name", "Database Table"}
    };
    
    auto tmpl = ResourceTemplate::from_json(json);
    
    REQUIRE(tmpl.uri_template == "db:///{table}");
    REQUIRE(tmpl.name == "Database Table");
    REQUIRE(!tmpl.description.has_value());
    REQUIRE(!tmpl.mime_type.has_value());
}

TEST_CASE("ListResourceTemplatesResult serialization", "[mcp][types][resource-template]") {
    ListResourceTemplatesResult result;
    result.resource_templates = {
        ResourceTemplate{"file:///{path}", "Files", std::nullopt, std::nullopt},
        ResourceTemplate{"db:///{table}", "Database", "Access tables", "application/json"}
    };
    result.next_cursor = "cursor123";
    
    auto json = result.to_json();
    
    REQUIRE(json["resourceTemplates"].is_array());
    REQUIRE(json["resourceTemplates"].size() == 2);
    REQUIRE(json["resourceTemplates"][0]["uriTemplate"] == "file:///{path}");
    REQUIRE(json["resourceTemplates"][1]["name"] == "Database");
    REQUIRE(json["nextCursor"] == "cursor123");
}

TEST_CASE("ListResourceTemplatesResult deserialization", "[mcp][types][resource-template]") {
    Json json = {
        {"resourceTemplates", Json::array({
            {{"uriTemplate", "s3:///{bucket}/{key}"}, {"name", "S3 Object"}},
            {{"uriTemplate", "git:///{repo}/{branch}"}, {"name", "Git Ref"}}
        })},
        {"nextCursor", "page2"}
    };
    
    auto result = ListResourceTemplatesResult::from_json(json);
    
    REQUIRE(result.resource_templates.size() == 2);
    REQUIRE(result.resource_templates[0].uri_template == "s3:///{bucket}/{key}");
    REQUIRE(result.resource_templates[1].name == "Git Ref");
    REQUIRE(result.next_cursor.value() == "page2");
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool Annotations Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ToolAnnotations serialization", "[mcp][types][tool-annotations]") {
    ToolAnnotations ann;
    ann.title = "Delete Files";
    ann.destructive_hint = true;
    ann.idempotent_hint = false;
    ann.read_only_hint = false;
    ann.open_world_hint = true;
    
    auto json = ann.to_json();
    
    REQUIRE(json["title"] == "Delete Files");
    REQUIRE(json["destructiveHint"] == true);
    REQUIRE(json["idempotentHint"] == false);
    REQUIRE(json["readOnlyHint"] == false);
    REQUIRE(json["openWorldHint"] == true);
}

TEST_CASE("ToolAnnotations deserialization", "[mcp][types][tool-annotations]") {
    Json json = {
        {"title", "Read Configuration"},
        {"destructiveHint", false},
        {"idempotentHint", true},
        {"readOnlyHint", true},
        {"openWorldHint", false}
    };
    
    auto ann = ToolAnnotations::from_json(json);
    
    REQUIRE(ann.title.value() == "Read Configuration");
    REQUIRE(ann.destructive_hint.value() == false);
    REQUIRE(ann.idempotent_hint.value() == true);
    REQUIRE(ann.read_only_hint.value() == true);
    REQUIRE(ann.open_world_hint.value() == false);
}

TEST_CASE("ToolAnnotations partial deserialization", "[mcp][types][tool-annotations]") {
    Json json = {
        {"destructiveHint", true}
    };
    
    auto ann = ToolAnnotations::from_json(json);
    
    REQUIRE(!ann.title.has_value());
    REQUIRE(ann.destructive_hint.value() == true);
    REQUIRE(!ann.idempotent_hint.has_value());
    REQUIRE(!ann.read_only_hint.has_value());
    REQUIRE(!ann.open_world_hint.has_value());
}

TEST_CASE("ToolAnnotations empty check", "[mcp][types][tool-annotations]") {
    ToolAnnotations empty_ann;
    REQUIRE(empty_ann.empty());
    
    ToolAnnotations non_empty;
    non_empty.read_only_hint = true;
    REQUIRE(!non_empty.empty());
}

TEST_CASE("Tool with annotations serialization", "[mcp][types][tool-annotations]") {
    Tool tool;
    tool.name = "delete_file";
    tool.description = "Permanently delete a file";
    tool.input_schema = {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}};
    tool.annotations = ToolAnnotations{};
    tool.annotations->title = "Delete File";
    tool.annotations->destructive_hint = true;
    tool.annotations->idempotent_hint = true;  // Deleting twice has same effect
    
    auto json = tool.to_json();
    
    REQUIRE(json["name"] == "delete_file");
    REQUIRE(json["description"] == "Permanently delete a file");
    REQUIRE(json.contains("annotations"));
    REQUIRE(json["annotations"]["destructiveHint"] == true);
    REQUIRE(json["annotations"]["idempotentHint"] == true);
}

TEST_CASE("Tool with annotations deserialization", "[mcp][types][tool-annotations]") {
    Json json = {
        {"name", "read_config"},
        {"description", "Read configuration file"},
        {"inputSchema", {{"type", "object"}}},
        {"annotations", {
            {"readOnlyHint", true},
            {"idempotentHint", true}
        }}
    };
    
    auto tool = Tool::from_json(json);
    
    REQUIRE(tool.name == "read_config");
    REQUIRE(tool.annotations.has_value());
    REQUIRE(tool.annotations->read_only_hint.value() == true);
    REQUIRE(tool.annotations->idempotent_hint.value() == true);
    REQUIRE(!tool.annotations->destructive_hint.has_value());
}

TEST_CASE("Tool without annotations", "[mcp][types][tool-annotations]") {
    Json json = {
        {"name", "simple_tool"},
        {"inputSchema", {{"type", "object"}}}
    };
    
    auto tool = Tool::from_json(json);
    
    REQUIRE(tool.name == "simple_tool");
    REQUIRE(!tool.annotations.has_value());
    
    // Serialization should not include annotations
    auto out_json = tool.to_json();
    REQUIRE(!out_json.contains("annotations"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Blob Resource Contents Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextResourceContents serialization", "[mcp][types][blob-resource]") {
    TextResourceContents contents;
    contents.uri = "file:///config.json";
    contents.mime_type = "application/json";
    contents.text = R"({"key": "value"})";
    
    auto json = contents.to_json();
    
    REQUIRE(json["uri"] == "file:///config.json");
    REQUIRE(json["mimeType"] == "application/json");
    REQUIRE(json["text"] == R"({"key": "value"})");
    REQUIRE(!json.contains("blob"));
}

TEST_CASE("TextResourceContents deserialization", "[mcp][types][blob-resource]") {
    Json json = {
        {"uri", "file:///readme.md"},
        {"mimeType", "text/markdown"},
        {"text", "# Hello World"}
    };
    
    auto contents = TextResourceContents::from_json(json);
    
    REQUIRE(contents.uri == "file:///readme.md");
    REQUIRE(contents.mime_type.value() == "text/markdown");
    REQUIRE(contents.text == "# Hello World");
    REQUIRE(contents.is_text());
    REQUIRE(!contents.is_blob());
}

TEST_CASE("BlobResourceContents serialization", "[mcp][types][blob-resource]") {
    BlobResourceContents contents;
    contents.uri = "file:///image.png";
    contents.mime_type = "image/png";
    contents.blob = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
    
    auto json = contents.to_json();
    
    REQUIRE(json["uri"] == "file:///image.png");
    REQUIRE(json["mimeType"] == "image/png");
    REQUIRE(json["blob"].get<std::string>().starts_with("iVBORw0K"));
    REQUIRE(!json.contains("text"));
}

TEST_CASE("BlobResourceContents deserialization", "[mcp][types][blob-resource]") {
    Json json = {
        {"uri", "file:///binary.dat"},
        {"mimeType", "application/octet-stream"},
        {"blob", "SGVsbG8gV29ybGQh"}  // "Hello World!" in base64
    };
    
    auto contents = BlobResourceContents::from_json(json);
    
    REQUIRE(contents.uri == "file:///binary.dat");
    REQUIRE(contents.mime_type.value() == "application/octet-stream");
    REQUIRE(contents.blob == "SGVsbG8gV29ybGQh");
    REQUIRE(!contents.is_text());
    REQUIRE(contents.is_blob());
}

TEST_CASE("ResourceContents text detection", "[mcp][types][blob-resource]") {
    Json json = {
        {"uri", "file:///test.txt"},
        {"text", "Hello"}
    };
    
    auto contents = ResourceContents::from_json(json);
    
    REQUIRE(contents.is_text());
    REQUIRE(!contents.is_blob());
    
    auto text_contents = contents.as_text();
    REQUIRE(text_contents.has_value());
    REQUIRE(text_contents->text == "Hello");
    
    auto blob_contents = contents.as_blob();
    REQUIRE(!blob_contents.has_value());
}

TEST_CASE("ResourceContents blob detection", "[mcp][types][blob-resource]") {
    Json json = {
        {"uri", "file:///test.bin"},
        {"blob", "AQIDBA=="}  // bytes 1,2,3,4 in base64
    };
    
    auto contents = ResourceContents::from_json(json);
    
    REQUIRE(!contents.is_text());
    REQUIRE(contents.is_blob());
    
    auto blob_contents = contents.as_blob();
    REQUIRE(blob_contents.has_value());
    REQUIRE(blob_contents->blob == "AQIDBA==");
    
    auto text_contents = contents.as_text();
    REQUIRE(!text_contents.has_value());
}

TEST_CASE("ResourceContents serialization roundtrip", "[mcp][types][blob-resource]") {
    ResourceContents original;
    original.uri = "file:///mixed.dat";
    original.mime_type = "application/octet-stream";
    original.blob = "dGVzdA==";  // "test" in base64
    
    auto json = original.to_json();
    auto restored = ResourceContents::from_json(json);
    
    REQUIRE(restored.uri == original.uri);
    REQUIRE(restored.mime_type == original.mime_type);
    REQUIRE(restored.blob == original.blob);
    REQUIRE(!restored.text.has_value());
}

