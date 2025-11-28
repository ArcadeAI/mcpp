# Example 05: Custom Handlers

Implement handlers for server-initiated requests: Elicitation, Sampling, and Roots.

## Handler Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                          MCP Server                              │
│                                                                  │
│  Server needs user input?  ──► elicitation/request              │
│  Server needs LLM?         ──► sampling/createMessage           │
│  Server needs file access? ──► roots/list                       │
└───────────────────────────────────┬─────────────────────────────┘
                                    │
                                    ▼
┌───────────────────────────────────────────────────────────────────┐
│                           McpClient                                │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │
│  │ IElicitation    │  │ ISampling       │  │ IRoots          │   │
│  │ Handler         │  │ Handler         │  │ Handler         │   │
│  │                 │  │                 │  │                 │   │
│  │ ┌─────────────┐ │  │ ┌─────────────┐ │  │ ┌─────────────┐ │   │
│  │ │ Your Code   │ │  │ │ Your Code   │ │  │ │ Your Code   │ │   │
│  │ │             │ │  │ │             │ │  │ │             │ │   │
│  │ │ Show form   │ │  │ │ Call LLM    │ │  │ │ Return dirs │ │   │
│  │ │ Open URL    │ │  │ │ Return resp │ │  │ │             │ │   │
│  │ └─────────────┘ │  │ └─────────────┘ │  │ └─────────────┘ │   │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘   │
└───────────────────────────────────────────────────────────────────┘
```

## Elicitation Handler

For gathering user input (forms, confirmations, URLs):

```cpp
class MyElicitationHandler : public IElicitationHandler {
public:
    ElicitationResult handle(const ElicitationRequest& req) override {
        // Check the mode
        if (req.mode == "url") {
            // Server wants us to open a URL
            std::cout << "Please visit: " << req.url.value_or("") << "\n";
            
            // Wait for user, then return
            return {ElicitationAction::Accepted};
        }
        
        if (req.mode == "form") {
            // Server wants form data
            ElicitationResult result;
            result.action = ElicitationAction::Accepted;
            
            // Fill in the requested fields
            for (const auto& field : req.schema.value_or(Json::object())) {
                // In real app, show UI and get user input
                result.data[field["name"]] = "user_input_here";
            }
            
            return result;
        }
        
        // Unknown mode
        return {ElicitationAction::Declined};
    }
};
```

## Sampling Handler

For providing LLM completions when server needs AI:

```cpp
class MySamplingHandler : public ISamplingHandler {
public:
    CreateMessageResult handle(const CreateMessageParams& params) override {
        // params contains:
        //   - messages: conversation history
        //   - model_preferences: hints about desired model
        //   - system_prompt: optional system message
        //   - max_tokens: token limit
        
        // Call your LLM API here
        std::string response = call_my_llm(params);
        
        CreateMessageResult result;
        result.role = "assistant";
        result.content = TextContent{response};
        result.model = "my-model-v1";
        
        return result;
    }
    
private:
    std::string call_my_llm(const CreateMessageParams& params) {
        // Your LLM integration here
        // Could be OpenAI, Anthropic, local model, etc.
        return "LLM response";
    }
};
```

## Roots Handler

For exposing filesystem directories to the server:

```cpp
// Option 1: Static roots (fixed directories)
auto handler = std::make_shared<StaticRootsHandler>(std::vector<Root>{
    {"/home/user/projects", "Projects"},
    {"/home/user/documents", "Documents"}
});

// Option 2: Mutable roots (can change at runtime)
auto handler = std::make_shared<MutableRootsHandler>();
handler->add_root("/home/user/projects", "Projects");

// Later...
handler->add_root("/tmp/workspace", "Temp Workspace");
handler->remove_root("/home/user/projects");

// Option 3: Custom dynamic handler
class MyRootsHandler : public IRootsHandler {
public:
    std::vector<Root> list_roots() override {
        // Return roots based on current state
        std::vector<Root> roots;
        
        // Maybe check user permissions, config, etc.
        if (user_has_access_to_projects()) {
            roots.push_back({"/projects", "Projects"});
        }
        
        return roots;
    }
};
```

## Registering Handlers

```cpp
McpClient client(std::move(transport));

// Set handlers before connecting
client.set_elicitation_handler(std::make_shared<MyElicitationHandler>());
client.set_sampling_handler(std::make_shared<MySamplingHandler>());
client.set_roots_handler(std::make_shared<StaticRootsHandler>(roots));

// Now connect - capabilities will be advertised
client.connect();
client.initialize();
```

## Client Capabilities

When you set handlers, the client advertises capabilities to the server:

```json
{
  "capabilities": {
    "elicitation": {},
    "sampling": {},
    "roots": {
      "listChanged": true
    }
  }
}
```

## Notifying Root Changes

When roots change, notify the server:

```cpp
auto roots_handler = std::make_shared<MutableRootsHandler>();
client.set_roots_handler(roots_handler);

// ... later, roots change ...
roots_handler->add_root("/new/path", "New Directory");

// Notify server
client.notify_roots_changed();
```

## Running

```bash
./custom_handlers_example
```

