#pragma once

#include "mcpp/protocol/mcp_types.hpp"

namespace mcpp {

// ═══════════════════════════════════════════════════════════════════════════
// Sampling Handler Interface
// ═══════════════════════════════════════════════════════════════════════════
//
// Implement this interface to handle sampling requests from MCP servers.
// Sampling allows servers to request LLM completions through your client.
//
// Human-in-the-Loop:
// ┌─────────────────────────────────────────────────────────────────────────┐
// │  The MCP spec recommends giving users the ability to:                   │
// │  1. Review and approve/modify prompts BEFORE sending to LLM            │
// │  2. Review and approve/modify responses BEFORE returning to server     │
// │                                                                         │
// │  Your handler implementation decides how much control to give users.    │
// └─────────────────────────────────────────────────────────────────────────┘
//
// Example Usage:
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                                                                         │
// │  // 1. Implement the handler                                            │
// │  class MyLlmHandler : public ISamplingHandler {                         │
// │  public:                                                                │
// │      std::optional<CreateMessageResult> handle_create_message(          │
// │          const CreateMessageParams& params                              │
// │      ) override {                                                       │
// │          // Optional: Show prompt to user for approval                  │
// │          if (!user_approves_prompt(params)) {                           │
// │              return std::nullopt;  // User declined                     │
// │          }                                                              │
// │                                                                         │
// │          // Call your LLM                                               │
// │          auto response = call_openai(params);                           │
// │                                                                         │
// │          // Optional: Let user review/modify response                   │
// │          response = user_review_response(response);                     │
// │                                                                         │
// │          return response;                                               │
// │      }                                                                  │
// │  };                                                                     │
// │                                                                         │
// │  // 2. Register with client                                             │
// │  auto handler = std::make_shared<MyLlmHandler>();                       │
// │  client.set_sampling_handler(handler);                                  │
// │                                                                         │
// └─────────────────────────────────────────────────────────────────────────┘

class ISamplingHandler {
public:
    virtual ~ISamplingHandler() = default;

    // ───────────────────────────────────────────────────────────────────────
    // Create Message Handler
    // ───────────────────────────────────────────────────────────────────────
    // Called when a server requests an LLM completion.
    //
    // Parameters:
    //   params - The sampling request containing:
    //     - messages: Conversation history
    //     - model_preferences: Hints about desired model capabilities
    //     - system_prompt: Optional system instruction
    //     - include_context: What MCP context to include
    //     - max_tokens: Token limit for response
    //     - stop_sequences: Strings that stop generation
    //     - metadata: Additional request metadata
    //
    // Returns:
    //   std::optional<CreateMessageResult>
    //     - Result with LLM response if successful
    //     - std::nullopt if user declined or error occurred
    //
    // Implementation Notes:
    //   - Consider showing the prompt to users before sending to LLM
    //   - Consider letting users modify the response before returning
    //   - Handle model_preferences.hints to select appropriate model
    //   - Respect max_tokens and stop_sequences
    //
    [[nodiscard]] virtual std::optional<CreateMessageResult> handle_create_message(
        const CreateMessageParams& params
    ) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Null Handler (Default)
// ═══════════════════════════════════════════════════════════════════════════
// Returns nullopt for all requests. Use when sampling is not supported.

class NullSamplingHandler : public ISamplingHandler {
public:
    [[nodiscard]] std::optional<CreateMessageResult> handle_create_message(
        const CreateMessageParams& /*params*/
    ) override {
        return std::nullopt;
    }
};

}  // namespace mcpp


