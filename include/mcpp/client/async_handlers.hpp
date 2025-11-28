#pragma once

#include "mcpp/protocol/mcp_types.hpp"

#include <asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace mcpp {

using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Async Elicitation Handler
// ═══════════════════════════════════════════════════════════════════════════
// Async version of IElicitationHandler for non-blocking UI operations.
// Use when:
// - Showing a dialog and waiting for user input
// - Making async HTTP calls
// - Any I/O that shouldn't block the event loop

class IAsyncElicitationHandler {
public:
    virtual ~IAsyncElicitationHandler() = default;

    /// Handle form-based elicitation (in-band)
    [[nodiscard]] virtual asio::awaitable<ElicitationResult> handle_form_async(
        const std::string& message,
        const Json& schema
    ) = 0;

    /// Handle URL-based elicitation (out-of-band)
    [[nodiscard]] virtual asio::awaitable<ElicitationResult> handle_url_async(
        const std::string& elicitation_id,
        const std::string& url,
        const std::string& message
    ) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Async Sampling Handler
// ═══════════════════════════════════════════════════════════════════════════
// Async version of ISamplingHandler for non-blocking LLM API calls.
// Use when:
// - Making HTTP calls to OpenAI/Anthropic/etc
// - Any async model inference

class IAsyncSamplingHandler {
public:
    virtual ~IAsyncSamplingHandler() = default;

    /// Handle sampling request asynchronously
    [[nodiscard]] virtual asio::awaitable<std::optional<CreateMessageResult>>
    handle_create_message_async(const CreateMessageParams& params) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Async Roots Handler
// ═══════════════════════════════════════════════════════════════════════════
// Async version of IRootsHandler for dynamic root discovery.
// Use when:
// - Scanning filesystem asynchronously
// - Querying remote services for available roots

class IAsyncRootsHandler {
public:
    virtual ~IAsyncRootsHandler() = default;

    /// List available roots asynchronously
    [[nodiscard]] virtual asio::awaitable<ListRootsResult> list_roots_async() = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Null Async Handlers (Default implementations)
// ═══════════════════════════════════════════════════════════════════════════

class NullAsyncElicitationHandler : public IAsyncElicitationHandler {
public:
    asio::awaitable<ElicitationResult> handle_form_async(
        const std::string& /*message*/,
        const Json& /*schema*/
    ) override {
        co_return ElicitationResult{ElicitationAction::Dismiss, std::nullopt};
    }

    asio::awaitable<ElicitationResult> handle_url_async(
        const std::string& /*elicitation_id*/,
        const std::string& /*url*/,
        const std::string& /*message*/
    ) override {
        co_return ElicitationResult{ElicitationAction::Dismiss, std::nullopt};
    }
};

class NullAsyncSamplingHandler : public IAsyncSamplingHandler {
public:
    asio::awaitable<std::optional<CreateMessageResult>> handle_create_message_async(
        const CreateMessageParams& /*params*/
    ) override {
        co_return std::nullopt;
    }
};

class NullAsyncRootsHandler : public IAsyncRootsHandler {
public:
    asio::awaitable<ListRootsResult> list_roots_async() override {
        co_return ListRootsResult{};
    }
};

}  // namespace mcpp


