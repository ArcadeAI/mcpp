#ifndef MCPP_PROTOCOL_MCP_TYPES_HPP
#define MCPP_PROTOCOL_MCP_TYPES_HPP

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>
#include <variant>

namespace mcpp {

using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// MCP Protocol Version
// ═══════════════════════════════════════════════════════════════════════════

inline constexpr const char* MCP_PROTOCOL_VERSION = "2024-11-05";

// ═══════════════════════════════════════════════════════════════════════════
// Request Metadata (_meta field)
// ═══════════════════════════════════════════════════════════════════════════
// Used to pass progress tokens and other metadata with requests.

using ProgressToken = std::variant<std::string, int>;

struct RequestMeta {
    std::optional<ProgressToken> progress_token;  // Token for progress notifications
    
    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        if (progress_token) {
            std::visit([&j](const auto& token) {
                j["progressToken"] = token;
            }, *progress_token);
        }
        return j;
    }
    
    [[nodiscard]] bool empty() const {
        return !progress_token.has_value();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Client/Server Info
// ═══════════════════════════════════════════════════════════════════════════

struct Implementation {
    std::string name;
    std::string version;

    [[nodiscard]] Json to_json() const {
        return {{"name", name}, {"version", version}};
    }

    static Implementation from_json(const Json& j) {
        return {
            j.value("name", ""),
            j.value("version", "")
        };
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation Capability (needed by ClientCapabilities)
// ═══════════════════════════════════════════════════════════════════════════

// Client capability declaration for elicitation
struct ElicitationCapability {
    bool form{true};   // Support form-based elicitation
    bool url{false};   // Support URL-based elicitation (SEP-1036)

    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        if (form) j["form"] = Json::object();
        if (url) j["url"] = Json::object();
        return j;
    }

    static ElicitationCapability from_json(const Json& j) {
        // Per spec: empty {} is equivalent to form-only for backwards compat
        return {
            j.contains("form") || j.empty(),
            j.contains("url")
        };
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Capabilities
// ═══════════════════════════════════════════════════════════════════════════

struct ClientCapabilities {
    struct Roots {
        bool list_changed = false;
    };
    struct Sampling {};

    std::optional<Roots> roots;
    std::optional<Sampling> sampling;
    std::optional<ElicitationCapability> elicitation;
    Json experimental;  // For future extensions

    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        if (roots) {
            j["roots"] = {{"listChanged", roots->list_changed}};
        }
        if (sampling) {
            j["sampling"] = Json::object();
        }
        if (elicitation) {
            j["elicitation"] = elicitation->to_json();
        }
        if (!experimental.empty()) {
            j["experimental"] = experimental;
        }
        return j;
    }
};

struct ServerCapabilities {
    struct Prompts {
        bool list_changed = false;
    };
    struct Resources {
        bool subscribe = false;
        bool list_changed = false;
    };
    struct Tools {
        bool list_changed = false;
    };
    struct Logging {};

    std::optional<Prompts> prompts;
    std::optional<Resources> resources;
    std::optional<Tools> tools;
    std::optional<Logging> logging;
    Json experimental;

    static ServerCapabilities from_json(const Json& j) {
        ServerCapabilities caps;
        if (j.contains("prompts")) {
            caps.prompts = Prompts{
                j["prompts"].value("listChanged", false)
            };
        }
        if (j.contains("resources")) {
            caps.resources = Resources{
                j["resources"].value("subscribe", false),
                j["resources"].value("listChanged", false)
            };
        }
        if (j.contains("tools")) {
            caps.tools = Tools{
                j["tools"].value("listChanged", false)
            };
        }
        if (j.contains("logging")) {
            caps.logging = Logging{};
        }
        if (j.contains("experimental")) {
            caps.experimental = j["experimental"];
        }
        return caps;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Initialize Request/Response
// ═══════════════════════════════════════════════════════════════════════════

struct InitializeParams {
    std::string protocol_version = MCP_PROTOCOL_VERSION;
    ClientCapabilities capabilities;
    Implementation client_info;

    [[nodiscard]] Json to_json() const {
        return {
            {"protocolVersion", protocol_version},
            {"capabilities", capabilities.to_json()},
            {"clientInfo", client_info.to_json()}
        };
    }
};

struct InitializeResult {
    std::string protocol_version;
    ServerCapabilities capabilities;
    Implementation server_info;
    std::optional<std::string> instructions;

    static InitializeResult from_json(const Json& j) {
        InitializeResult result;
        result.protocol_version = j.value("protocolVersion", "");
        if (j.contains("capabilities")) {
            result.capabilities = ServerCapabilities::from_json(j["capabilities"]);
        }
        if (j.contains("serverInfo")) {
            result.server_info = Implementation::from_json(j["serverInfo"]);
        }
        if (j.contains("instructions")) {
            result.instructions = j["instructions"].get<std::string>();
        }
        return result;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Tools
// ═══════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Tool Annotations
// ─────────────────────────────────────────────────────────────────────────────
// Hints about tool behavior for clients to make informed decisions.

struct ToolAnnotations {
    // Tool's potential impact: "low", "medium", "high"
    std::optional<std::string> title;
    
    // If true, tool may modify external state (files, databases, APIs)
    std::optional<bool> destructive_hint;
    
    // If true, repeated calls with same args produce same result
    std::optional<bool> idempotent_hint;
    
    // If true, tool only reads data without side effects
    std::optional<bool> read_only_hint;
    
    // If true, tool may interact with external systems (network, APIs)
    std::optional<bool> open_world_hint;
    
    [[nodiscard]] static ToolAnnotations from_json(const Json& j) {
        ToolAnnotations ann;
        if (j.contains("title")) {
            ann.title = j["title"].get<std::string>();
        }
        if (j.contains("destructiveHint")) {
            ann.destructive_hint = j["destructiveHint"].get<bool>();
        }
        if (j.contains("idempotentHint")) {
            ann.idempotent_hint = j["idempotentHint"].get<bool>();
        }
        if (j.contains("readOnlyHint")) {
            ann.read_only_hint = j["readOnlyHint"].get<bool>();
        }
        if (j.contains("openWorldHint")) {
            ann.open_world_hint = j["openWorldHint"].get<bool>();
        }
        return ann;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        if (title) j["title"] = *title;
        if (destructive_hint) j["destructiveHint"] = *destructive_hint;
        if (idempotent_hint) j["idempotentHint"] = *idempotent_hint;
        if (read_only_hint) j["readOnlyHint"] = *read_only_hint;
        if (open_world_hint) j["openWorldHint"] = *open_world_hint;
        return j;
    }
    
    [[nodiscard]] bool empty() const {
        return !title && !destructive_hint && !idempotent_hint && 
               !read_only_hint && !open_world_hint;
    }
};

struct Tool {
    std::string name;
    std::optional<std::string> description;
    Json input_schema;  // JSON Schema for tool arguments
    std::optional<ToolAnnotations> annotations;  // Hints about tool behavior

    static Tool from_json(const Json& j) {
        Tool tool;
        tool.name = j.value("name", "");
        if (j.contains("description")) {
            tool.description = j["description"].get<std::string>();
        }
        if (j.contains("inputSchema")) {
            tool.input_schema = j["inputSchema"];
        }
        if (j.contains("annotations")) {
            tool.annotations = ToolAnnotations::from_json(j["annotations"]);
        }
        return tool;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {{"name", name}};
        if (description) {
            j["description"] = *description;
        }
        if (!input_schema.empty()) {
            j["inputSchema"] = input_schema;
        }
        if (annotations && !annotations->empty()) {
            j["annotations"] = annotations->to_json();
        }
        return j;
    }
};

struct ListToolsResult {
    std::vector<Tool> tools;
    std::optional<std::string> next_cursor;

    static ListToolsResult from_json(const Json& j) {
        ListToolsResult result;
        if (j.contains("tools") && j["tools"].is_array()) {
            for (const auto& t : j["tools"]) {
                result.tools.push_back(Tool::from_json(t));
            }
        }
        if (j.contains("nextCursor")) {
            result.next_cursor = j["nextCursor"].get<std::string>();
        }
        return result;
    }
};

struct CallToolParams {
    std::string name;
    Json arguments;
    std::optional<RequestMeta> meta;  // _meta field for progress tokens etc.

    [[nodiscard]] Json to_json() const {
        Json j = {{"name", name}};
        if (!arguments.empty()) {
            j["arguments"] = arguments;
        }
        if (meta && !meta->empty()) {
            j["_meta"] = meta->to_json();
        }
        return j;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Content Types (used in tool results, prompts, etc.)
// ═══════════════════════════════════════════════════════════════════════════

struct TextContent {
    std::string text;
    std::optional<Json> annotations;

    static TextContent from_json(const Json& j) {
        TextContent content;
        content.text = j.value("text", "");
        if (j.contains("annotations")) {
            content.annotations = j["annotations"];
        }
        return content;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {{"type", "text"}, {"text", text}};
        if (annotations) {
            j["annotations"] = *annotations;
        }
        return j;
    }
};

struct ImageContent {
    std::string data;      // Base64 encoded
    std::string mime_type;
    std::optional<Json> annotations;

    static ImageContent from_json(const Json& j) {
        ImageContent content;
        content.data = j.value("data", "");
        content.mime_type = j.value("mimeType", "");
        if (j.contains("annotations")) {
            content.annotations = j["annotations"];
        }
        return content;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {
            {"type", "image"},
            {"data", data},
            {"mimeType", mime_type}
        };
        if (annotations) {
            j["annotations"] = *annotations;
        }
        return j;
    }
};

struct AudioContent {
    std::string data;      // Base64 encoded
    std::string mime_type;
    std::optional<Json> annotations;

    static AudioContent from_json(const Json& j) {
        AudioContent content;
        content.data = j.value("data", "");
        content.mime_type = j.value("mimeType", "");
        if (j.contains("annotations")) {
            content.annotations = j["annotations"];
        }
        return content;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {
            {"type", "audio"},
            {"data", data},
            {"mimeType", mime_type}
        };
        if (annotations) {
            j["annotations"] = *annotations;
        }
        return j;
    }
};

struct EmbeddedResource {
    std::string uri;
    std::optional<std::string> mime_type;
    std::optional<std::string> text;
    std::optional<std::string> blob;  // Base64 encoded

    static EmbeddedResource from_json(const Json& j) {
        EmbeddedResource res;
        if (j.contains("resource")) {
            const auto& r = j["resource"];
            res.uri = r.value("uri", "");
            if (r.contains("mimeType")) {
                res.mime_type = r["mimeType"].get<std::string>();
            }
            if (r.contains("text")) {
                res.text = r["text"].get<std::string>();
            }
            if (r.contains("blob")) {
                res.blob = r["blob"].get<std::string>();
            }
        }
        return res;
    }
};

using Content = std::variant<TextContent, ImageContent, EmbeddedResource>;

struct CallToolResult {
    std::vector<Content> content;
    bool is_error = false;

    static CallToolResult from_json(const Json& j) {
        CallToolResult result;
        result.is_error = j.value("isError", false);
        
        if (j.contains("content") && j["content"].is_array()) {
            for (const auto& c : j["content"]) {
                const auto type = c.value("type", "");
                if (type == "text") {
                    result.content.push_back(TextContent::from_json(c));
                } else if (type == "image") {
                    result.content.push_back(ImageContent::from_json(c));
                } else if (type == "resource") {
                    result.content.push_back(EmbeddedResource::from_json(c));
                }
            }
        }
        return result;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Resources
// ═══════════════════════════════════════════════════════════════════════════

struct Resource {
    std::string uri;
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;

    static Resource from_json(const Json& j) {
        Resource res;
        res.uri = j.value("uri", "");
        res.name = j.value("name", "");
        if (j.contains("description")) {
            res.description = j["description"].get<std::string>();
        }
        if (j.contains("mimeType")) {
            res.mime_type = j["mimeType"].get<std::string>();
        }
        return res;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {{"uri", uri}, {"name", name}};
        if (description) j["description"] = *description;
        if (mime_type) j["mimeType"] = *mime_type;
        return j;
    }
};

struct ListResourcesResult {
    std::vector<Resource> resources;
    std::optional<std::string> next_cursor;

    static ListResourcesResult from_json(const Json& j) {
        ListResourcesResult result;
        if (j.contains("resources") && j["resources"].is_array()) {
            for (const auto& r : j["resources"]) {
                result.resources.push_back(Resource::from_json(r));
            }
        }
        if (j.contains("nextCursor")) {
            result.next_cursor = j["nextCursor"].get<std::string>();
        }
        return result;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Resource Contents Types
// ─────────────────────────────────────────────────────────────────────────────
// Resources can contain either text or binary (blob) content.
// TextResourceContents: UTF-8 text content
// BlobResourceContents: Base64-encoded binary content

struct TextResourceContents {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string text;
    
    [[nodiscard]] static TextResourceContents from_json(const Json& j) {
        TextResourceContents contents;
        contents.uri = j.value("uri", "");
        if (j.contains("mimeType")) {
            contents.mime_type = j["mimeType"].get<std::string>();
        }
        contents.text = j.value("text", "");
        return contents;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j = {{"uri", uri}, {"text", text}};
        if (mime_type) j["mimeType"] = *mime_type;
        return j;
    }
    
    [[nodiscard]] bool is_text() const { return true; }
    [[nodiscard]] bool is_blob() const { return false; }
};

struct BlobResourceContents {
    std::string uri;
    std::optional<std::string> mime_type;
    std::string blob;  // Base64 encoded binary data
    
    [[nodiscard]] static BlobResourceContents from_json(const Json& j) {
        BlobResourceContents contents;
        contents.uri = j.value("uri", "");
        if (j.contains("mimeType")) {
            contents.mime_type = j["mimeType"].get<std::string>();
        }
        contents.blob = j.value("blob", "");
        return contents;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j = {{"uri", uri}, {"blob", blob}};
        if (mime_type) j["mimeType"] = *mime_type;
        return j;
    }
    
    [[nodiscard]] bool is_text() const { return false; }
    [[nodiscard]] bool is_blob() const { return true; }
};

// Union type for resource contents (backwards compatible)
struct ResourceContents {
    std::string uri;
    std::optional<std::string> mime_type;
    std::optional<std::string> text;
    std::optional<std::string> blob;  // Base64 encoded

    static ResourceContents from_json(const Json& j) {
        ResourceContents contents;
        contents.uri = j.value("uri", "");
        if (j.contains("mimeType")) {
            contents.mime_type = j["mimeType"].get<std::string>();
        }
        if (j.contains("text")) {
            contents.text = j["text"].get<std::string>();
        }
        if (j.contains("blob")) {
            contents.blob = j["blob"].get<std::string>();
        }
        return contents;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j = {{"uri", uri}};
        if (mime_type) j["mimeType"] = *mime_type;
        if (text) j["text"] = *text;
        if (blob) j["blob"] = *blob;
        return j;
    }
    
    [[nodiscard]] bool is_text() const { return text.has_value(); }
    [[nodiscard]] bool is_blob() const { return blob.has_value(); }
    
    // Convert to typed version
    [[nodiscard]] std::optional<TextResourceContents> as_text() const {
        if (!text) return std::nullopt;
        return TextResourceContents{uri, mime_type, *text};
    }
    
    [[nodiscard]] std::optional<BlobResourceContents> as_blob() const {
        if (!blob) return std::nullopt;
        return BlobResourceContents{uri, mime_type, *blob};
    }
};

struct ReadResourceResult {
    std::vector<ResourceContents> contents;

    static ReadResourceResult from_json(const Json& j) {
        ReadResourceResult result;
        if (j.contains("contents") && j["contents"].is_array()) {
            for (const auto& c : j["contents"]) {
                result.contents.push_back(ResourceContents::from_json(c));
            }
        }
        return result;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Resource Subscriptions
// ─────────────────────────────────────────────────────────────────────────────
// Allow clients to subscribe to resource changes.
// MCP methods: resources/subscribe, resources/unsubscribe
// MCP notification: notifications/resources/updated

struct SubscribeResourceParams {
    std::string uri;
    
    [[nodiscard]] static SubscribeResourceParams from_json(const Json& j) {
        return SubscribeResourceParams{j.value("uri", "")};
    }
    
    [[nodiscard]] Json to_json() const {
        return {{"uri", uri}};
    }
};

struct UnsubscribeResourceParams {
    std::string uri;
    
    [[nodiscard]] static UnsubscribeResourceParams from_json(const Json& j) {
        return UnsubscribeResourceParams{j.value("uri", "")};
    }
    
    [[nodiscard]] Json to_json() const {
        return {{"uri", uri}};
    }
};

struct ResourceUpdatedNotification {
    std::string uri;
    
    [[nodiscard]] static ResourceUpdatedNotification from_json(const Json& j) {
        return ResourceUpdatedNotification{j.value("uri", "")};
    }
    
    [[nodiscard]] Json to_json() const {
        return {{"uri", uri}};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Resource Templates
// ─────────────────────────────────────────────────────────────────────────────
// URI templates for dynamic resources (RFC 6570).
// MCP method: resources/templates/list

struct ResourceTemplate {
    std::string uri_template;  // RFC 6570 URI template (e.g., "file:///{path}")
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> mime_type;
    
    [[nodiscard]] static ResourceTemplate from_json(const Json& j) {
        ResourceTemplate tmpl;
        tmpl.uri_template = j.value("uriTemplate", "");
        tmpl.name = j.value("name", "");
        if (j.contains("description")) {
            tmpl.description = j["description"].get<std::string>();
        }
        if (j.contains("mimeType")) {
            tmpl.mime_type = j["mimeType"].get<std::string>();
        }
        return tmpl;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j = {
            {"uriTemplate", uri_template},
            {"name", name}
        };
        if (description) j["description"] = *description;
        if (mime_type) j["mimeType"] = *mime_type;
        return j;
    }
};

struct ListResourceTemplatesResult {
    std::vector<ResourceTemplate> resource_templates;
    std::optional<std::string> next_cursor;
    
    [[nodiscard]] static ListResourceTemplatesResult from_json(const Json& j) {
        ListResourceTemplatesResult result;
        if (j.contains("resourceTemplates") && j["resourceTemplates"].is_array()) {
            for (const auto& t : j["resourceTemplates"]) {
                result.resource_templates.push_back(ResourceTemplate::from_json(t));
            }
        }
        if (j.contains("nextCursor")) {
            result.next_cursor = j["nextCursor"].get<std::string>();
        }
        return result;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        Json templates_arr = Json::array();
        for (const auto& t : resource_templates) {
            templates_arr.push_back(t.to_json());
        }
        j["resourceTemplates"] = templates_arr;
        if (next_cursor) j["nextCursor"] = *next_cursor;
        return j;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Prompts
// ═══════════════════════════════════════════════════════════════════════════

struct PromptArgument {
    std::string name;
    std::optional<std::string> description;
    bool required = false;

    static PromptArgument from_json(const Json& j) {
        PromptArgument arg;
        arg.name = j.value("name", "");
        if (j.contains("description")) {
            arg.description = j["description"].get<std::string>();
        }
        arg.required = j.value("required", false);
        return arg;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {{"name", name}};
        if (description) j["description"] = *description;
        if (required) j["required"] = required;
        return j;
    }
};

struct Prompt {
    std::string name;
    std::optional<std::string> description;
    std::vector<PromptArgument> arguments;

    static Prompt from_json(const Json& j) {
        Prompt prompt;
        prompt.name = j.value("name", "");
        if (j.contains("description")) {
            prompt.description = j["description"].get<std::string>();
        }
        if (j.contains("arguments") && j["arguments"].is_array()) {
            for (const auto& a : j["arguments"]) {
                prompt.arguments.push_back(PromptArgument::from_json(a));
            }
        }
        return prompt;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {{"name", name}};
        if (description) j["description"] = *description;
        if (!arguments.empty()) {
            j["arguments"] = Json::array();
            for (const auto& arg : arguments) {
                j["arguments"].push_back(arg.to_json());
            }
        }
        return j;
    }
};

struct ListPromptsResult {
    std::vector<Prompt> prompts;
    std::optional<std::string> next_cursor;

    static ListPromptsResult from_json(const Json& j) {
        ListPromptsResult result;
        if (j.contains("prompts") && j["prompts"].is_array()) {
            for (const auto& p : j["prompts"]) {
                result.prompts.push_back(Prompt::from_json(p));
            }
        }
        if (j.contains("nextCursor")) {
            result.next_cursor = j["nextCursor"].get<std::string>();
        }
        return result;
    }
};

struct PromptMessage {
    std::string role;  // "user" or "assistant"
    Content content;

    static PromptMessage from_json(const Json& j) {
        PromptMessage msg;
        msg.role = j.value("role", "");
        if (j.contains("content")) {
            const auto& c = j["content"];
            const auto type = c.value("type", "text");
            if (type == "text") {
                msg.content = TextContent::from_json(c);
            } else if (type == "image") {
                msg.content = ImageContent::from_json(c);
            } else if (type == "resource") {
                msg.content = EmbeddedResource::from_json(c);
            }
        }
        return msg;
    }
};

struct GetPromptResult {
    std::optional<std::string> description;
    std::vector<PromptMessage> messages;

    static GetPromptResult from_json(const Json& j) {
        GetPromptResult result;
        if (j.contains("description")) {
            result.description = j["description"].get<std::string>();
        }
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& m : j["messages"]) {
                result.messages.push_back(PromptMessage::from_json(m));
            }
        }
        return result;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Completion
// ═══════════════════════════════════════════════════════════════════════════
// Request autocompletion suggestions for prompts or resources.
// MCP method: completion/complete

enum class CompletionRefType {
    Prompt,
    Resource
};

inline std::string completion_ref_type_to_string(CompletionRefType type) {
    switch (type) {
        case CompletionRefType::Prompt: return "ref/prompt";
        case CompletionRefType::Resource: return "ref/resource";
    }
    return "ref/prompt";
}

inline CompletionRefType completion_ref_type_from_string(const std::string& s) {
    if (s == "ref/resource") return CompletionRefType::Resource;
    return CompletionRefType::Prompt;
}

struct CompletionReference {
    CompletionRefType type;
    std::string name;  // Prompt or resource name/URI
    
    [[nodiscard]] static CompletionReference from_json(const Json& j) {
        CompletionReference ref;
        ref.type = completion_ref_type_from_string(j.value("type", "ref/prompt"));
        ref.name = j.value("name", "");
        return ref;
    }
    
    [[nodiscard]] Json to_json() const {
        return {
            {"type", completion_ref_type_to_string(type)},
            {"name", name}
        };
    }
};

struct CompletionArgument {
    std::string name;
    std::string value;
    
    [[nodiscard]] static CompletionArgument from_json(const Json& j) {
        return CompletionArgument{
            j.value("name", ""),
            j.value("value", "")
        };
    }
    
    [[nodiscard]] Json to_json() const {
        return {{"name", name}, {"value", value}};
    }
};

struct CompleteParams {
    CompletionReference ref;
    CompletionArgument argument;
    
    [[nodiscard]] static CompleteParams from_json(const Json& j) {
        CompleteParams params;
        if (j.contains("ref")) {
            params.ref = CompletionReference::from_json(j["ref"]);
        }
        if (j.contains("argument")) {
            params.argument = CompletionArgument::from_json(j["argument"]);
        }
        return params;
    }
    
    [[nodiscard]] Json to_json() const {
        return {
            {"ref", ref.to_json()},
            {"argument", argument.to_json()}
        };
    }
};

struct CompletionInfo {
    std::vector<std::string> values;
    std::optional<int> total;
    bool has_more{false};
    
    [[nodiscard]] static CompletionInfo from_json(const Json& j) {
        CompletionInfo info;
        if (j.contains("values") && j["values"].is_array()) {
            for (const auto& v : j["values"]) {
                if (v.is_string()) {
                    info.values.push_back(v.get<std::string>());
                }
            }
        }
        if (j.contains("total") && j["total"].is_number_integer()) {
            info.total = j["total"].get<int>();
        }
        info.has_more = j.value("hasMore", false);
        return info;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j;
        j["values"] = values;
        if (total) j["total"] = *total;
        if (has_more) j["hasMore"] = has_more;
        return j;
    }
};

struct CompleteResult {
    CompletionInfo completion;
    
    [[nodiscard]] static CompleteResult from_json(const Json& j) {
        CompleteResult result;
        if (j.contains("completion")) {
            result.completion = CompletionInfo::from_json(j["completion"]);
        }
        return result;
    }
    
    [[nodiscard]] Json to_json() const {
        return {{"completion", completion.to_json()}};
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Logging
// ═══════════════════════════════════════════════════════════════════════════

enum class LoggingLevel {
    Debug,
    Info,
    Notice,
    Warning,
    Error,
    Critical,
    Alert,
    Emergency
};

inline std::string to_string(LoggingLevel level) {
    switch (level) {
        case LoggingLevel::Debug: return "debug";
        case LoggingLevel::Info: return "info";
        case LoggingLevel::Notice: return "notice";
        case LoggingLevel::Warning: return "warning";
        case LoggingLevel::Error: return "error";
        case LoggingLevel::Critical: return "critical";
        case LoggingLevel::Alert: return "alert";
        case LoggingLevel::Emergency: return "emergency";
    }
    return "info";
}

inline LoggingLevel logging_level_from_string(const std::string& s) {
    if (s == "debug") return LoggingLevel::Debug;
    if (s == "info") return LoggingLevel::Info;
    if (s == "notice") return LoggingLevel::Notice;
    if (s == "warning") return LoggingLevel::Warning;
    if (s == "error") return LoggingLevel::Error;
    if (s == "critical") return LoggingLevel::Critical;
    if (s == "alert") return LoggingLevel::Alert;
    if (s == "emergency") return LoggingLevel::Emergency;
    return LoggingLevel::Info;
}

// Note: logging_level_to_string is an alias for to_string(LoggingLevel)
// Kept for backward compatibility
inline std::string logging_level_to_string(LoggingLevel level) {
    return to_string(level);
}

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation (MCP 2025-06-18)
// ═══════════════════════════════════════════════════════════════════════════
//
// Elicitation enables servers to request information from users through the
// client. Two modes are supported:
//
//   Form Mode (in-band):  Data collected via UI forms, passes through client
//   URL Mode (out-of-band): User redirected to browser, data bypasses client
//
// Flow:
// ┌────────┐                    ┌────────┐                    ┌────────┐
// │ Server │───elicitation/────▶│ Client │───show form/url───▶│  User  │
// │        │    create          │        │◀──user response────│        │
// │        │◀───result──────────│        │                    │        │
// └────────┘                    └────────┘                    └────────┘

enum class ElicitationMode {
    Form,  // In-band data collection via structured forms
    Url    // Out-of-band interaction via browser (SEP-1036)
};

[[nodiscard]] inline std::string to_string(ElicitationMode mode) {
    switch (mode) {
        case ElicitationMode::Form: return "form";
        case ElicitationMode::Url: return "url";
    }
    return "form";
}

[[nodiscard]] inline ElicitationMode elicitation_mode_from_string(const std::string& s) {
    if (s == "url") return ElicitationMode::Url;
    return ElicitationMode::Form;  // Default
}

enum class ElicitationAction {
    Accept,   // User provided data (form mode)
    Decline,  // User declined to provide data
    Dismiss,  // User dismissed without responding
    Opened    // User opened the URL (url mode)
};

[[nodiscard]] inline std::string to_string(ElicitationAction action) {
    switch (action) {
        case ElicitationAction::Accept: return "accept";
        case ElicitationAction::Decline: return "decline";
        case ElicitationAction::Dismiss: return "dismiss";
        case ElicitationAction::Opened: return "opened";
    }
    return "dismiss";
}

[[nodiscard]] inline ElicitationAction elicitation_action_from_string(const std::string& s) {
    if (s == "accept") return ElicitationAction::Accept;
    if (s == "decline") return ElicitationAction::Decline;
    if (s == "opened") return ElicitationAction::Opened;
    return ElicitationAction::Dismiss;  // Default
}


// Form mode elicitation request (server → client)
struct FormElicitationParams {
    std::string message;          // Human-readable explanation
    Json requested_schema;        // JSON Schema for validation

    [[nodiscard]] Json to_json() const {
        return {
            {"mode", "form"},
            {"message", message},
            {"requestedSchema", requested_schema}
        };
    }

    static FormElicitationParams from_json(const Json& j) {
        return {
            j.value("message", ""),
            j.value("requestedSchema", Json::object())
        };
    }
};

// URL mode elicitation request (server → client) - SEP-1036
struct UrlElicitationParams {
    std::string elicitation_id;   // Unique identifier for tracking
    std::string url;              // HTTPS URL to open in browser
    std::string message;          // Human-readable explanation

    [[nodiscard]] Json to_json() const {
        return {
            {"mode", "url"},
            {"elicitationId", elicitation_id},
            {"url", url},
            {"message", message}
        };
    }

    static UrlElicitationParams from_json(const Json& j) {
        return {
            j.value("elicitationId", ""),
            j.value("url", ""),
            j.value("message", "")
        };
    }
};

// Elicitation result (client → server)
struct ElicitationResult {
    ElicitationAction action;
    std::optional<Json> content;  // User-provided data (Accept action only)

    [[nodiscard]] Json to_json() const {
        Json j = {{"action", to_string(action)}};
        if (content) j["content"] = *content;
        return j;
    }

    static ElicitationResult from_json(const Json& j) {
        ElicitationResult result;
        result.action = elicitation_action_from_string(j.value("action", ""));
        if (j.contains("content")) {
            result.content = j["content"];
        }
        return result;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Sampling (MCP 2025-06-18)
// ═══════════════════════════════════════════════════════════════════════════
//
// Sampling enables servers to request LLM completions through the client.
// This allows agentic behaviors where servers leverage the client's AI model.
//
// Flow (Human-in-the-Loop):
// ┌────────┐         ┌────────┐         ┌────────┐         ┌────────┐
// │ Server │────────▶│ Client │────────▶│  User  │────────▶│  LLM   │
// │        │ create  │        │ review? │        │ approve │        │
// │        │ Message │        │         │        │         │        │
// │        │◀────────│        │◀────────│        │◀────────│        │
// └────────┘ result  └────────┘         └────────┘         └────────┘

enum class SamplingRole {
    User,
    Assistant
};

[[nodiscard]] inline std::string to_string(SamplingRole role) {
    switch (role) {
        case SamplingRole::User: return "user";
        case SamplingRole::Assistant: return "assistant";
    }
    return "user";
}

[[nodiscard]] inline SamplingRole sampling_role_from_string(const std::string& s) {
    if (s == "assistant") return SamplingRole::Assistant;
    return SamplingRole::User;  // Default
}

enum class StopReason {
    EndTurn,
    StopSequence,
    MaxTokens
};

[[nodiscard]] inline std::string to_string(StopReason reason) {
    switch (reason) {
        case StopReason::EndTurn: return "endTurn";
        case StopReason::StopSequence: return "stopSequence";
        case StopReason::MaxTokens: return "maxTokens";
    }
    return "endTurn";
}

[[nodiscard]] inline StopReason stop_reason_from_string(const std::string& s) {
    if (s == "stopSequence") return StopReason::StopSequence;
    if (s == "maxTokens") return StopReason::MaxTokens;
    return StopReason::EndTurn;  // Default
}

enum class IncludeContext {
    None,
    ThisServer,
    AllServers
};

[[nodiscard]] inline std::string to_string(IncludeContext ctx) {
    switch (ctx) {
        case IncludeContext::None: return "none";
        case IncludeContext::ThisServer: return "thisServer";
        case IncludeContext::AllServers: return "allServers";
    }
    return "none";
}

[[nodiscard]] inline IncludeContext include_context_from_string(const std::string& s) {
    if (s == "thisServer") return IncludeContext::ThisServer;
    if (s == "allServers") return IncludeContext::AllServers;
    return IncludeContext::None;  // Default
}

// ───────────────────────────────────────────────────────────────────────────
// Sampling Content (reuses TextContent, ImageContent from above + AudioContent)
// ───────────────────────────────────────────────────────────────────────────

using SamplingContent = std::variant<TextContent, ImageContent, AudioContent>;

[[nodiscard]] inline Json sampling_content_to_json(const SamplingContent& content) {
    return std::visit([](const auto& c) { return c.to_json(); }, content);
}

[[nodiscard]] inline SamplingContent sampling_content_from_json(const Json& j) {
    std::string type = j.value("type", "text");
    if (type == "image") return ImageContent::from_json(j);
    if (type == "audio") return AudioContent::from_json(j);
    return TextContent::from_json(j);  // Default to text
}

// ───────────────────────────────────────────────────────────────────────────
// Sampling Message
// ───────────────────────────────────────────────────────────────────────────

struct SamplingMessage {
    SamplingRole role;
    SamplingContent content;

    [[nodiscard]] Json to_json() const {
        return {
            {"role", to_string(role)},
            {"content", sampling_content_to_json(content)}
        };
    }

    static SamplingMessage from_json(const Json& j) {
        return {
            sampling_role_from_string(j.value("role", "")),
            sampling_content_from_json(j.value("content", Json::object()))
        };
    }
};

// ───────────────────────────────────────────────────────────────────────────
// Model Preferences
// ───────────────────────────────────────────────────────────────────────────

struct ModelHint {
    std::optional<std::string> name;

    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        if (name) j["name"] = *name;
        return j;
    }

    static ModelHint from_json(const Json& j) {
        ModelHint hint;
        if (j.contains("name")) hint.name = j["name"].get<std::string>();
        return hint;
    }
};

struct ModelPreferences {
    std::vector<ModelHint> hints;
    std::optional<double> cost_priority;         // 0.0 - 1.0
    std::optional<double> speed_priority;        // 0.0 - 1.0
    std::optional<double> intelligence_priority; // 0.0 - 1.0

    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        if (!hints.empty()) {
            j["hints"] = Json::array();
            for (const auto& h : hints) {
                j["hints"].push_back(h.to_json());
            }
        }
        if (cost_priority) j["costPriority"] = *cost_priority;
        if (speed_priority) j["speedPriority"] = *speed_priority;
        if (intelligence_priority) j["intelligencePriority"] = *intelligence_priority;
        return j;
    }

    static ModelPreferences from_json(const Json& j) {
        ModelPreferences prefs;
        if (j.contains("hints") && j["hints"].is_array()) {
            for (const auto& h : j["hints"]) {
                prefs.hints.push_back(ModelHint::from_json(h));
            }
        }
        if (j.contains("costPriority")) {
            prefs.cost_priority = j["costPriority"].get<double>();
        }
        if (j.contains("speedPriority")) {
            prefs.speed_priority = j["speedPriority"].get<double>();
        }
        if (j.contains("intelligencePriority")) {
            prefs.intelligence_priority = j["intelligencePriority"].get<double>();
        }
        return prefs;
    }
};

// ───────────────────────────────────────────────────────────────────────────
// Create Message Request/Response
// ───────────────────────────────────────────────────────────────────────────

struct CreateMessageParams {
    std::vector<SamplingMessage> messages;
    std::optional<ModelPreferences> model_preferences;
    std::optional<std::string> system_prompt;
    IncludeContext include_context{IncludeContext::None};
    std::optional<int> max_tokens;
    std::vector<std::string> stop_sequences;
    std::optional<Json> metadata;

    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        j["messages"] = Json::array();
        for (const auto& m : messages) {
            j["messages"].push_back(m.to_json());
        }
        if (model_preferences) j["modelPreferences"] = model_preferences->to_json();
        if (system_prompt) j["systemPrompt"] = *system_prompt;
        if (include_context != IncludeContext::None) {
            j["includeContext"] = to_string(include_context);
        }
        if (max_tokens) j["maxTokens"] = *max_tokens;
        if (!stop_sequences.empty()) j["stopSequences"] = stop_sequences;
        if (metadata) j["metadata"] = *metadata;
        return j;
    }

    static CreateMessageParams from_json(const Json& j) {
        CreateMessageParams params;
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& m : j["messages"]) {
                params.messages.push_back(SamplingMessage::from_json(m));
            }
        }
        if (j.contains("modelPreferences")) {
            params.model_preferences = ModelPreferences::from_json(j["modelPreferences"]);
        }
        if (j.contains("systemPrompt")) {
            params.system_prompt = j["systemPrompt"].get<std::string>();
        }
        if (j.contains("includeContext")) {
            params.include_context = include_context_from_string(j["includeContext"].get<std::string>());
        }
        if (j.contains("maxTokens")) {
            params.max_tokens = j["maxTokens"].get<int>();
        }
        if (j.contains("stopSequences") && j["stopSequences"].is_array()) {
            params.stop_sequences = j["stopSequences"].get<std::vector<std::string>>();
        }
        if (j.contains("metadata")) {
            params.metadata = j["metadata"];
        }
        return params;
    }
};

struct CreateMessageResult {
    SamplingRole role;
    SamplingContent content;
    std::string model;
    StopReason stop_reason;

    [[nodiscard]] Json to_json() const {
        return {
            {"role", to_string(role)},
            {"content", sampling_content_to_json(content)},
            {"model", model},
            {"stopReason", to_string(stop_reason)}
        };
    }

    static CreateMessageResult from_json(const Json& j) {
        return {
            sampling_role_from_string(j.value("role", "")),
            sampling_content_from_json(j.value("content", Json::object())),
            j.value("model", ""),
            stop_reason_from_string(j.value("stopReason", ""))
        };
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Roots (MCP 2025-06-18)
// ═══════════════════════════════════════════════════════════════════════════
//
// Roots define filesystem boundaries that the client exposes to servers.
// Servers use roots to understand which directories/files they can access.
//
// Flow:
// ┌────────┐                    ┌────────┐
// │ Server │───roots/list──────▶│ Client │
// │        │◀──list of roots────│        │
// │        │                    │        │
// │        │◀─roots_changed─────│        │  (notification when roots change)
// └────────┘                    └────────┘

struct Root {
    std::string uri;                   // file:// URI pointing to root directory
    std::optional<std::string> name;   // Human-readable name for display

    [[nodiscard]] Json to_json() const {
        Json j = {{"uri", uri}};
        if (name) j["name"] = *name;
        return j;
    }

    static Root from_json(const Json& j) {
        Root root;
        root.uri = j.value("uri", "");
        if (j.contains("name")) {
            root.name = j["name"].get<std::string>();
        }
        return root;
    }
};

struct ListRootsResult {
    std::vector<Root> roots;

    [[nodiscard]] Json to_json() const {
        Json j = Json::object();
        j["roots"] = Json::array();
        for (const auto& r : roots) {
            j["roots"].push_back(r.to_json());
        }
        return j;
    }

    static ListRootsResult from_json(const Json& j) {
        ListRootsResult result;
        if (j.contains("roots") && j["roots"].is_array()) {
            for (const auto& r : j["roots"]) {
                result.roots.push_back(Root::from_json(r));
            }
        }
        return result;
    }
};

struct RootsCapability {
    bool list_changed{false};  // Client can notify when roots change

    [[nodiscard]] Json to_json() const {
        return {{"listChanged", list_changed}};
    }

    static RootsCapability from_json(const Json& j) {
        return {j.value("listChanged", false)};
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Errors
// ═══════════════════════════════════════════════════════════════════════════

struct McpError {
    int code;
    std::string message;
    std::optional<Json> data;

    static McpError from_json(const Json& j) {
        McpError err;
        err.code = j.value("code", 0);
        err.message = j.value("message", "");
        if (j.contains("data")) {
            err.data = j["data"];
        }
        return err;
    }

    [[nodiscard]] Json to_json() const {
        Json j = {{"code", code}, {"message", message}};
        if (data) j["data"] = *data;
        return j;
    }
};

// Standard JSON-RPC error codes
namespace ErrorCode {
    inline constexpr int ParseError = -32700;
    inline constexpr int InvalidRequest = -32600;
    inline constexpr int MethodNotFound = -32601;
    inline constexpr int InvalidParams = -32602;
    inline constexpr int InternalError = -32603;
}

// ═══════════════════════════════════════════════════════════════════════════
// Ping
// ═══════════════════════════════════════════════════════════════════════════
// Health check mechanism. Both client and server can send ping requests.
// MCP method: "ping"

struct PingResult {
    // Empty result - ping just confirms connectivity
    
    [[nodiscard]] static PingResult from_json(const Json& /*j*/) {
        return PingResult{};
    }
    
    [[nodiscard]] Json to_json() const {
        return Json::object();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Cancellation
// ═══════════════════════════════════════════════════════════════════════════
// Cancel in-progress operations.
// MCP notification: "notifications/cancelled"

struct CancelledNotification {
    std::variant<std::string, int> request_id{0};  // ID of request to cancel (default: 0)
    std::optional<std::string> reason;              // Why it was cancelled
    
    [[nodiscard]] static CancelledNotification from_json(const Json& j) {
        CancelledNotification result;
        
        // requestId is required per MCP spec, but we handle missing gracefully
        if (j.contains("requestId")) {
            if (j["requestId"].is_string()) {
                result.request_id = j["requestId"].get<std::string>();
            } else if (j["requestId"].is_number_integer()) {
                result.request_id = j["requestId"].get<int>();
            }
            // else: keep default value
        }
        
        if (j.contains("reason") && j["reason"].is_string()) {
            result.reason = j["reason"].get<std::string>();
        }
        
        return result;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j;
        
        if (std::holds_alternative<std::string>(request_id)) {
            j["requestId"] = std::get<std::string>(request_id);
        } else {
            j["requestId"] = std::get<int>(request_id);
        }
        
        if (reason) {
            j["reason"] = *reason;
        }
        
        return j;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Logging Control
// ═══════════════════════════════════════════════════════════════════════════
// Control server logging level.
// MCP method: "logging/setLevel"

struct SetLoggingLevelParams {
    LoggingLevel level;
    
    [[nodiscard]] static SetLoggingLevelParams from_json(const Json& j) {
        SetLoggingLevelParams result;
        result.level = logging_level_from_string(j.value("level", "info"));
        return result;
    }
    
    [[nodiscard]] Json to_json() const {
        return {{"level", logging_level_to_string(level)}};
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Progress Notifications
// ═══════════════════════════════════════════════════════════════════════════
// Track progress of long-running operations.
// MCP notification: "notifications/progress"
// Note: ProgressToken is defined earlier in this file near RequestMeta.

struct ProgressNotification {
    ProgressToken progress_token{0};    // Token identifying the operation (default: 0)
    double progress{0.0};               // Current progress value
    std::optional<double> total;        // Total for percentage calculation
    
    [[nodiscard]] static ProgressNotification from_json(const Json& j) {
        ProgressNotification result;
        
        // progressToken is required per MCP spec, but we handle missing gracefully
        if (j.contains("progressToken")) {
            if (j["progressToken"].is_string()) {
                result.progress_token = j["progressToken"].get<std::string>();
            } else if (j["progressToken"].is_number_integer()) {
                result.progress_token = j["progressToken"].get<int>();
            }
            // else: keep default value
        }
        
        result.progress = j.value("progress", 0.0);
        
        if (j.contains("total") && j["total"].is_number()) {
            result.total = j["total"].get<double>();
        }
        
        return result;
    }
    
    [[nodiscard]] Json to_json() const {
        Json j;
        
        if (std::holds_alternative<std::string>(progress_token)) {
            j["progressToken"] = std::get<std::string>(progress_token);
        } else {
            j["progressToken"] = std::get<int>(progress_token);
        }
        
        j["progress"] = progress;
        
        if (total) {
            j["total"] = *total;
        }
        
        return j;
    }
    
    // Helper to calculate percentage
    [[nodiscard]] std::optional<double> percentage() const {
        if (total && *total > 0) {
            return (progress / *total) * 100.0;
        }
        return std::nullopt;
    }
};

}  // namespace mcpp

#endif  // MCPP_PROTOCOL_MCP_TYPES_HPP

