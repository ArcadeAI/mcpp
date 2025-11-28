#pragma once

#include "mcpp/protocol/mcp_types.hpp"

namespace mcpp {

// ═══════════════════════════════════════════════════════════════════════════
// Elicitation Handler Interface
// ═══════════════════════════════════════════════════════════════════════════
//
// Implement this interface to handle elicitation requests from MCP servers.
// The client library calls your implementation when a server needs user input.
//
// Example Usage:
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                                                                         │
// │  // 1. Implement the handler                                            │
// │  class MyCliHandler : public IElicitationHandler {                      │
// │  public:                                                                │
// │      ElicitationResult handle_form(...) override {                      │
// │          std::cout << message << std::endl;                             │
// │          std::string input;                                             │
// │          std::getline(std::cin, input);                                 │
// │          return {ElicitationAction::Accept, Json{{"value", input}}};    │
// │      }                                                                  │
// │                                                                         │
// │      ElicitationResult handle_url(...) override {                       │
// │          std::cout << "Open: " << url << std::endl;                     │
// │          return {ElicitationAction::Opened, std::nullopt};              │
// │      }                                                                  │
// │  };                                                                     │
// │                                                                         │
// │  // 2. Register with client                                             │
// │  auto handler = std::make_shared<MyCliHandler>();                       │
// │  client.set_elicitation_handler(handler);                               │
// │                                                                         │
// └─────────────────────────────────────────────────────────────────────────┘

class IElicitationHandler {
public:
    virtual ~IElicitationHandler() = default;

    // ───────────────────────────────────────────────────────────────────────
    // Form Mode Handler
    // ───────────────────────────────────────────────────────────────────────
    // Called when server requests structured data via a form.
    // 
    // Parameters:
    //   message - Human-readable explanation of what data is needed
    //   schema  - JSON Schema describing expected input format
    //
    // Returns:
    //   ElicitationResult with action and optional content
    //
    // Expected actions:
    //   Accept  - User provided data (content must be set)
    //   Decline - User refused to provide data
    //   Dismiss - User dismissed the form
    //
    [[nodiscard]] virtual ElicitationResult handle_form(
        const std::string& message,
        const Json& schema
    ) = 0;

    // ───────────────────────────────────────────────────────────────────────
    // URL Mode Handler (SEP-1036)
    // ───────────────────────────────────────────────────────────────────────
    // Called when server requests out-of-band interaction via browser.
    // Used for sensitive operations (OAuth, payments, API keys).
    //
    // Parameters:
    //   elicitation_id - Unique ID for tracking this elicitation
    //   url            - HTTPS URL to open in user's browser
    //   message        - Human-readable explanation
    //
    // Returns:
    //   ElicitationResult with action (content is typically empty)
    //
    // Expected actions:
    //   Opened  - User opened the URL in browser
    //   Decline - User refused to open the URL
    //   Dismiss - User dismissed the request
    //
    // Security Notes:
    //   - Only HTTPS URLs should be accepted
    //   - Display the domain to user before opening
    //   - Never auto-open URLs without user consent
    //
    [[nodiscard]] virtual ElicitationResult handle_url(
        const std::string& elicitation_id,
        const std::string& url,
        const std::string& message
    ) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Null Handler (Default)
// ═══════════════════════════════════════════════════════════════════════════
// Returns Dismiss for all requests. Use when elicitation is not supported.

class NullElicitationHandler : public IElicitationHandler {
public:
    [[nodiscard]] ElicitationResult handle_form(
        const std::string& /*message*/,
        const Json& /*schema*/
    ) override {
        return {ElicitationAction::Dismiss, std::nullopt};
    }

    [[nodiscard]] ElicitationResult handle_url(
        const std::string& /*elicitation_id*/,
        const std::string& /*url*/,
        const std::string& /*message*/
    ) override {
        return {ElicitationAction::Dismiss, std::nullopt};
    }
};

}  // namespace mcpp


