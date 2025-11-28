// ─────────────────────────────────────────────────────────────────────────────
// Additional MCP Features Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for Ping, Cancellation, Progress, and Async Handlers

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mcpp/protocol/mcp_types.hpp"
#include "mcpp/client/async_handlers.hpp"

using namespace mcpp;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Ping Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PingResult serialization", "[mcp][ping]") {
    PingResult result;
    
    Json j = result.to_json();
    REQUIRE(j.is_object());
    REQUIRE(j.empty());
    
    auto parsed = PingResult::from_json(j);
    // PingResult has no fields, just confirms it parses
}

// ═══════════════════════════════════════════════════════════════════════════
// Cancellation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CancelledNotification with string ID", "[mcp][cancel]") {
    CancelledNotification notification;
    notification.request_id = std::string("req-123");
    notification.reason = "User cancelled";
    
    Json j = notification.to_json();
    REQUIRE(j["requestId"] == "req-123");
    REQUIRE(j["reason"] == "User cancelled");
    
    auto parsed = CancelledNotification::from_json(j);
    REQUIRE(std::holds_alternative<std::string>(parsed.request_id));
    REQUIRE(std::get<std::string>(parsed.request_id) == "req-123");
    REQUIRE(parsed.reason == "User cancelled");
}

TEST_CASE("CancelledNotification with int ID", "[mcp][cancel]") {
    CancelledNotification notification;
    notification.request_id = 42;
    
    Json j = notification.to_json();
    REQUIRE(j["requestId"] == 42);
    REQUIRE_FALSE(j.contains("reason"));
    
    auto parsed = CancelledNotification::from_json(j);
    REQUIRE(std::holds_alternative<int>(parsed.request_id));
    REQUIRE(std::get<int>(parsed.request_id) == 42);
    REQUIRE_FALSE(parsed.reason.has_value());
}

TEST_CASE("CancelledNotification with missing requestId uses default", "[mcp][cancel]") {
    Json j = Json::object();  // Empty JSON
    
    auto parsed = CancelledNotification::from_json(j);
    // Should default to int 0
    REQUIRE(std::holds_alternative<int>(parsed.request_id));
    REQUIRE(std::get<int>(parsed.request_id) == 0);
}

TEST_CASE("CancelledNotification with invalid requestId type uses default", "[mcp][cancel]") {
    Json j = {{"requestId", 3.14}};  // Float instead of int/string
    
    auto parsed = CancelledNotification::from_json(j);
    // Should keep default (int 0) when type is wrong
    REQUIRE(std::holds_alternative<int>(parsed.request_id));
    REQUIRE(std::get<int>(parsed.request_id) == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// Logging Control Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SetLoggingLevelParams serialization", "[mcp][logging]") {
    SetLoggingLevelParams params;
    params.level = LoggingLevel::Warning;
    
    Json j = params.to_json();
    REQUIRE(j["level"] == "warning");
    
    auto parsed = SetLoggingLevelParams::from_json(j);
    REQUIRE(parsed.level == LoggingLevel::Warning);
}

TEST_CASE("SetLoggingLevelParams all levels", "[mcp][logging]") {
    std::vector<std::pair<LoggingLevel, std::string>> levels = {
        {LoggingLevel::Debug, "debug"},
        {LoggingLevel::Info, "info"},
        {LoggingLevel::Notice, "notice"},
        {LoggingLevel::Warning, "warning"},
        {LoggingLevel::Error, "error"},
        {LoggingLevel::Critical, "critical"},
        {LoggingLevel::Alert, "alert"},
        {LoggingLevel::Emergency, "emergency"}
    };
    
    for (const auto& [level, str] : levels) {
        SetLoggingLevelParams params;
        params.level = level;
        
        Json j = params.to_json();
        REQUIRE(j["level"] == str);
        
        auto parsed = SetLoggingLevelParams::from_json(j);
        REQUIRE(parsed.level == level);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Progress Notification Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProgressNotification with string token", "[mcp][progress]") {
    ProgressNotification notification;
    notification.progress_token = std::string("task-1");
    notification.progress = 50.0;
    notification.total = 100.0;
    
    Json j = notification.to_json();
    REQUIRE(j["progressToken"] == "task-1");
    REQUIRE(j["progress"] == 50.0);
    REQUIRE(j["total"] == 100.0);
    
    auto parsed = ProgressNotification::from_json(j);
    REQUIRE(std::holds_alternative<std::string>(parsed.progress_token));
    REQUIRE(std::get<std::string>(parsed.progress_token) == "task-1");
    REQUIRE(parsed.progress == Catch::Approx(50.0));
    REQUIRE(parsed.total.has_value());
    REQUIRE(*parsed.total == Catch::Approx(100.0));
}

TEST_CASE("ProgressNotification with int token", "[mcp][progress]") {
    ProgressNotification notification;
    notification.progress_token = 123;
    notification.progress = 25.0;
    
    Json j = notification.to_json();
    REQUIRE(j["progressToken"] == 123);
    REQUIRE(j["progress"] == 25.0);
    REQUIRE_FALSE(j.contains("total"));
    
    auto parsed = ProgressNotification::from_json(j);
    REQUIRE(std::holds_alternative<int>(parsed.progress_token));
    REQUIRE(std::get<int>(parsed.progress_token) == 123);
    REQUIRE_FALSE(parsed.total.has_value());
}

TEST_CASE("ProgressNotification with missing progressToken uses default", "[mcp][progress]") {
    Json j = {{"progress", 50.0}};  // No progressToken
    
    auto parsed = ProgressNotification::from_json(j);
    // Should default to int 0
    REQUIRE(std::holds_alternative<int>(parsed.progress_token));
    REQUIRE(std::get<int>(parsed.progress_token) == 0);
    REQUIRE(parsed.progress == Catch::Approx(50.0));
}

TEST_CASE("ProgressNotification with invalid total type ignores it", "[mcp][progress]") {
    Json j = {{"progressToken", 1}, {"progress", 30.0}, {"total", "invalid"}};
    
    auto parsed = ProgressNotification::from_json(j);
    REQUIRE_FALSE(parsed.total.has_value());  // String "invalid" should be ignored
}

TEST_CASE("ProgressNotification percentage calculation", "[mcp][progress]") {
    SECTION("With total") {
        ProgressNotification notification;
        notification.progress_token = 1;
        notification.progress = 30.0;
        notification.total = 100.0;
        
        auto pct = notification.percentage();
        REQUIRE(pct.has_value());
        REQUIRE(*pct == Catch::Approx(30.0));
    }
    
    SECTION("Without total") {
        ProgressNotification notification;
        notification.progress_token = 1;
        notification.progress = 30.0;
        
        auto pct = notification.percentage();
        REQUIRE_FALSE(pct.has_value());
    }
    
    SECTION("Zero total") {
        ProgressNotification notification;
        notification.progress_token = 1;
        notification.progress = 30.0;
        notification.total = 0.0;
        
        auto pct = notification.percentage();
        REQUIRE_FALSE(pct.has_value());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Async Handler Interface Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NullAsyncElicitationHandler returns dismiss", "[async][handlers]") {
    // Note: Can't easily test coroutines without io_context
    // This just verifies the types compile correctly
    NullAsyncElicitationHandler handler;
    // Type check - handler is valid
    IAsyncElicitationHandler* base = &handler;
    REQUIRE(base != nullptr);
}

TEST_CASE("NullAsyncSamplingHandler returns nullopt", "[async][handlers]") {
    NullAsyncSamplingHandler handler;
    IAsyncSamplingHandler* base = &handler;
    REQUIRE(base != nullptr);
}

TEST_CASE("NullAsyncRootsHandler returns empty", "[async][handlers]") {
    NullAsyncRootsHandler handler;
    IAsyncRootsHandler* base = &handler;
    REQUIRE(base != nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// Resource Subscription Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SubscribeResourceParams serialization", "[mcp][resources][subscribe]") {
    SubscribeResourceParams params;
    params.uri = "file:///path/to/resource.txt";
    
    Json j = params.to_json();
    REQUIRE(j["uri"] == "file:///path/to/resource.txt");
    
    auto parsed = SubscribeResourceParams::from_json(j);
    REQUIRE(parsed.uri == "file:///path/to/resource.txt");
}

TEST_CASE("UnsubscribeResourceParams serialization", "[mcp][resources][subscribe]") {
    UnsubscribeResourceParams params;
    params.uri = "file:///path/to/resource.txt";
    
    Json j = params.to_json();
    REQUIRE(j["uri"] == "file:///path/to/resource.txt");
    
    auto parsed = UnsubscribeResourceParams::from_json(j);
    REQUIRE(parsed.uri == "file:///path/to/resource.txt");
}

TEST_CASE("ResourceUpdatedNotification serialization", "[mcp][resources][subscribe]") {
    ResourceUpdatedNotification notification;
    notification.uri = "file:///changed/resource.md";
    
    Json j = notification.to_json();
    REQUIRE(j["uri"] == "file:///changed/resource.md");
    
    auto parsed = ResourceUpdatedNotification::from_json(j);
    REQUIRE(parsed.uri == "file:///changed/resource.md");
}

TEST_CASE("ResourceUpdatedNotification with empty uri", "[mcp][resources][subscribe]") {
    Json j = Json::object();  // Empty
    
    auto parsed = ResourceUpdatedNotification::from_json(j);
    REQUIRE(parsed.uri.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Completion API Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("CompletionRefType conversion", "[mcp][completion]") {
    REQUIRE(completion_ref_type_to_string(CompletionRefType::Prompt) == "ref/prompt");
    REQUIRE(completion_ref_type_to_string(CompletionRefType::Resource) == "ref/resource");
    
    REQUIRE(completion_ref_type_from_string("ref/prompt") == CompletionRefType::Prompt);
    REQUIRE(completion_ref_type_from_string("ref/resource") == CompletionRefType::Resource);
    REQUIRE(completion_ref_type_from_string("unknown") == CompletionRefType::Prompt);  // Default
}

TEST_CASE("CompletionReference serialization", "[mcp][completion]") {
    CompletionReference ref;
    ref.type = CompletionRefType::Prompt;
    ref.name = "my_prompt";
    
    Json j = ref.to_json();
    REQUIRE(j["type"] == "ref/prompt");
    REQUIRE(j["name"] == "my_prompt");
    
    auto parsed = CompletionReference::from_json(j);
    REQUIRE(parsed.type == CompletionRefType::Prompt);
    REQUIRE(parsed.name == "my_prompt");
}

TEST_CASE("CompletionReference for resource", "[mcp][completion]") {
    CompletionReference ref;
    ref.type = CompletionRefType::Resource;
    ref.name = "file:///path/to/file.txt";
    
    Json j = ref.to_json();
    REQUIRE(j["type"] == "ref/resource");
    REQUIRE(j["name"] == "file:///path/to/file.txt");
}

TEST_CASE("CompletionArgument serialization", "[mcp][completion]") {
    CompletionArgument arg;
    arg.name = "query";
    arg.value = "hel";
    
    Json j = arg.to_json();
    REQUIRE(j["name"] == "query");
    REQUIRE(j["value"] == "hel");
    
    auto parsed = CompletionArgument::from_json(j);
    REQUIRE(parsed.name == "query");
    REQUIRE(parsed.value == "hel");
}

TEST_CASE("CompleteParams serialization", "[mcp][completion]") {
    CompleteParams params;
    params.ref = CompletionReference{CompletionRefType::Prompt, "search"};
    params.argument = CompletionArgument{"query", "test"};
    
    Json j = params.to_json();
    REQUIRE(j["ref"]["type"] == "ref/prompt");
    REQUIRE(j["ref"]["name"] == "search");
    REQUIRE(j["argument"]["name"] == "query");
    REQUIRE(j["argument"]["value"] == "test");
    
    auto parsed = CompleteParams::from_json(j);
    REQUIRE(parsed.ref.type == CompletionRefType::Prompt);
    REQUIRE(parsed.ref.name == "search");
    REQUIRE(parsed.argument.name == "query");
    REQUIRE(parsed.argument.value == "test");
}

TEST_CASE("CompletionInfo serialization", "[mcp][completion]") {
    CompletionInfo info;
    info.values = {"hello", "help", "helicopter"};
    info.total = 10;
    info.has_more = true;
    
    Json j = info.to_json();
    REQUIRE(j["values"].size() == 3);
    REQUIRE(j["values"][0] == "hello");
    REQUIRE(j["values"][1] == "help");
    REQUIRE(j["values"][2] == "helicopter");
    REQUIRE(j["total"] == 10);
    REQUIRE(j["hasMore"] == true);
    
    auto parsed = CompletionInfo::from_json(j);
    REQUIRE(parsed.values.size() == 3);
    REQUIRE(parsed.values[0] == "hello");
    REQUIRE(parsed.total.has_value());
    REQUIRE(*parsed.total == 10);
    REQUIRE(parsed.has_more == true);
}

TEST_CASE("CompletionInfo without optional fields", "[mcp][completion]") {
    CompletionInfo info;
    info.values = {"one", "two"};
    // No total, has_more defaults to false
    
    Json j = info.to_json();
    REQUIRE(j["values"].size() == 2);
    REQUIRE_FALSE(j.contains("total"));
    REQUIRE_FALSE(j.contains("hasMore"));  // false is not serialized
    
    auto parsed = CompletionInfo::from_json(j);
    REQUIRE(parsed.values.size() == 2);
    REQUIRE_FALSE(parsed.total.has_value());
    REQUIRE(parsed.has_more == false);
}

TEST_CASE("CompleteResult serialization", "[mcp][completion]") {
    CompleteResult result;
    result.completion.values = {"apple", "apricot"};
    result.completion.total = 5;
    result.completion.has_more = true;
    
    Json j = result.to_json();
    REQUIRE(j["completion"]["values"].size() == 2);
    REQUIRE(j["completion"]["total"] == 5);
    REQUIRE(j["completion"]["hasMore"] == true);
    
    auto parsed = CompleteResult::from_json(j);
    REQUIRE(parsed.completion.values.size() == 2);
    REQUIRE(parsed.completion.values[0] == "apple");
    REQUIRE(*parsed.completion.total == 5);
    REQUIRE(parsed.completion.has_more == true);
}

TEST_CASE("CompleteResult from empty JSON", "[mcp][completion]") {
    Json j = Json::object();
    
    auto parsed = CompleteResult::from_json(j);
    REQUIRE(parsed.completion.values.empty());
    REQUIRE_FALSE(parsed.completion.total.has_value());
    REQUIRE(parsed.completion.has_more == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Request Metadata / Progress Token Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RequestMeta with string progress token", "[mcp][progress]") {
    RequestMeta meta;
    meta.progress_token = std::string("my-progress-token");
    
    Json j = meta.to_json();
    REQUIRE(j.contains("progressToken"));
    REQUIRE(j["progressToken"] == "my-progress-token");
    REQUIRE_FALSE(meta.empty());
}

TEST_CASE("RequestMeta with int progress token", "[mcp][progress]") {
    RequestMeta meta;
    meta.progress_token = 42;
    
    Json j = meta.to_json();
    REQUIRE(j.contains("progressToken"));
    REQUIRE(j["progressToken"] == 42);
    REQUIRE_FALSE(meta.empty());
}

TEST_CASE("RequestMeta empty when no progress token", "[mcp][progress]") {
    RequestMeta meta;
    
    REQUIRE(meta.empty());
    
    Json j = meta.to_json();
    REQUIRE(j.is_object());
    REQUIRE_FALSE(j.contains("progressToken"));
}

TEST_CASE("CallToolParams with progress token", "[mcp][progress]") {
    CallToolParams params;
    params.name = "my_tool";
    params.arguments = {{"arg1", "value1"}};
    params.meta = RequestMeta{.progress_token = std::string("tool-progress-123")};
    
    Json j = params.to_json();
    
    REQUIRE(j["name"] == "my_tool");
    REQUIRE(j["arguments"]["arg1"] == "value1");
    REQUIRE(j.contains("_meta"));
    REQUIRE(j["_meta"]["progressToken"] == "tool-progress-123");
}

TEST_CASE("CallToolParams without progress token", "[mcp][progress]") {
    CallToolParams params;
    params.name = "simple_tool";
    params.arguments = {{"x", 1}};
    // No meta set
    
    Json j = params.to_json();
    
    REQUIRE(j["name"] == "simple_tool");
    REQUIRE(j["arguments"]["x"] == 1);
    REQUIRE_FALSE(j.contains("_meta"));
}

TEST_CASE("CallToolParams with empty meta", "[mcp][progress]") {
    CallToolParams params;
    params.name = "tool";
    params.meta = RequestMeta{};  // Empty meta
    
    Json j = params.to_json();
    
    REQUIRE(j["name"] == "tool");
    REQUIRE_FALSE(j.contains("_meta"));  // Empty meta should not be serialized
}

TEST_CASE("Progress token as int in CallToolParams", "[mcp][progress]") {
    CallToolParams params;
    params.name = "counter_tool";
    params.meta = RequestMeta{.progress_token = 999};
    
    Json j = params.to_json();
    
    REQUIRE(j["_meta"]["progressToken"] == 999);
}

