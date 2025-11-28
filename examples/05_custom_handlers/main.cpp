// Example 05: Custom Handlers
// 
// Implement handlers for server-initiated requests.

#include <mcpp/transport/process_transport.hpp>
#include <mcpp/client/mcp_client.hpp>
#include <mcpp/client/elicitation_handler.hpp>
#include <mcpp/client/sampling_handler.hpp>
#include <mcpp/client/roots_handler.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace mcpp;
using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Custom Elicitation Handler
// ═══════════════════════════════════════════════════════════════════════════

class ConsoleElicitationHandler : public IElicitationHandler {
public:
    ElicitationResult handle(const ElicitationRequest& req) override {
        std::cout << "\n[Elicitation Request]\n";
        std::cout << "  Message: " << req.message << "\n";
        std::cout << "  Mode: " << req.mode << "\n";
        
        if (req.mode == "url") {
            // URL mode - server wants us to open a URL
            if (req.url) {
                std::cout << "  URL: " << *req.url << "\n";
                std::cout << "\nPlease visit the URL above and press Enter when done...\n";
                std::cin.get();
            }
            return {ElicitationAction::Accepted};
        }
        
        if (req.mode == "form") {
            // Form mode - server wants structured input
            std::cout << "  (Form mode - would show UI here)\n";
            
            ElicitationResult result;
            result.action = ElicitationAction::Accepted;
            result.data = Json::object();
            
            // In a real app, you'd show a form UI
            // For demo, just return dummy data
            if (req.schema) {
                std::cout << "  Schema: " << req.schema->dump(2) << "\n";
            }
            
            return result;
        }
        
        std::cout << "  (Unknown mode - declining)\n";
        return {ElicitationAction::Declined};
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Custom Sampling Handler
// ═══════════════════════════════════════════════════════════════════════════

class MockSamplingHandler : public ISamplingHandler {
public:
    CreateMessageResult handle(const CreateMessageParams& params) override {
        std::cout << "\n[Sampling Request]\n";
        std::cout << "  Messages: " << params.messages.size() << "\n";
        
        if (params.system_prompt) {
            std::cout << "  System: " << *params.system_prompt << "\n";
        }
        
        if (params.max_tokens) {
            std::cout << "  Max tokens: " << *params.max_tokens << "\n";
        }
        
        // In a real app, you'd call your LLM here
        // For demo, return a mock response
        
        CreateMessageResult result;
        result.role = "assistant";
        result.model = "mock-model-v1";
        
        // Create text content
        TextContent text;
        text.type = "text";
        text.text = "This is a mock LLM response. In a real implementation, "
                    "you would call your LLM API (OpenAI, Anthropic, etc.) here.";
        result.content = text;
        
        std::cout << "  Returning mock response\n";
        
        return result;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "=== Custom Handlers Example ===\n\n";
    
    // 1. Create handlers
    auto elicitation_handler = std::make_shared<ConsoleElicitationHandler>();
    auto sampling_handler = std::make_shared<MockSamplingHandler>();
    
    // Use mutable roots handler so we can modify at runtime
    auto roots_handler = std::make_shared<MutableRootsHandler>();
    roots_handler->add_root("/tmp", "Temporary Files");
    roots_handler->add_root("/home", "Home Directories");
    
    std::cout << "Created handlers:\n";
    std::cout << "  - ConsoleElicitationHandler (shows prompts in console)\n";
    std::cout << "  - MockSamplingHandler (returns mock LLM responses)\n";
    std::cout << "  - MutableRootsHandler (dynamic root directories)\n\n";
    
    // 2. Configure transport
    ProcessTransportConfig config;
    config.command = "npx";
    config.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
    
    auto transport = std::make_unique<ProcessTransport>(config);
    if (!transport->start()) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    
    // 3. Create client and set handlers BEFORE connecting
    McpClient client(std::move(transport));
    
    client.set_elicitation_handler(elicitation_handler);
    client.set_sampling_handler(sampling_handler);
    client.set_roots_handler(roots_handler);
    
    std::cout << "Handlers registered with client\n\n";
    
    // 4. Connect and initialize
    if (!client.connect()) {
        std::cerr << "Failed to connect\n";
        return 1;
    }
    
    auto init = client.initialize();
    if (!init) {
        std::cerr << "Failed to initialize: " << init.error().message << "\n";
        return 1;
    }
    
    std::cout << "Connected to: " << init->server_info.name << "\n\n";
    
    // 5. Show what capabilities were advertised
    std::cout << "=== Client Capabilities Advertised ===\n";
    std::cout << "  elicitation: enabled (ConsoleElicitationHandler)\n";
    std::cout << "  sampling: enabled (MockSamplingHandler)\n";
    std::cout << "  roots: enabled with listChanged\n\n";
    
    // 6. Demonstrate roots handler
    std::cout << "=== Current Roots ===\n";
    auto roots = roots_handler->list_roots();
    for (const auto& root : roots) {
        std::cout << "  - " << root.uri;
        if (root.name) {
            std::cout << " (" << *root.name << ")";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    
    // 7. Modify roots at runtime
    std::cout << "=== Modifying Roots ===\n";
    roots_handler->add_root("/var/log", "System Logs");
    std::cout << "Added: /var/log (System Logs)\n";
    
    roots_handler->remove_root("/home");
    std::cout << "Removed: /home\n";
    
    // Notify server of changes
    auto notify_result = client.notify_roots_changed();
    if (notify_result) {
        std::cout << "Server notified of root changes\n";
    } else {
        std::cout << "Note: Server may not support roots notifications\n";
    }
    std::cout << "\n";
    
    // 8. Show updated roots
    std::cout << "=== Updated Roots ===\n";
    roots = roots_handler->list_roots();
    for (const auto& root : roots) {
        std::cout << "  - " << root.uri;
        if (root.name) {
            std::cout << " (" << *root.name << ")";
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    
    // 9. Note about elicitation and sampling
    std::cout << "=== Handler Notes ===\n";
    std::cout << "Elicitation and Sampling handlers are called by the SERVER\n";
    std::cout << "when it needs user input or LLM assistance. The filesystem\n";
    std::cout << "server doesn't use these, but other servers (like AI agents)\n";
    std::cout << "will trigger them during complex operations.\n\n";
    
    // 10. Cleanup
    std::cout << "Disconnecting...\n";
    client.disconnect();
    std::cout << "Done!\n";
    
    return 0;
}

