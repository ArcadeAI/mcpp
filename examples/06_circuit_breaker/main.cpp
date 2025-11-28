// Example 06: Circuit Breaker
// 
// Demonstrates resilience patterns with circuit breaker.

#include <mcpp/transport/process_transport.hpp>
#include <mcpp/client/mcp_client.hpp>
#include <mcpp/resilience/circuit_breaker.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace mcpp;
using Json = nlohmann::json;

std::string state_to_string(CircuitState state) {
    switch (state) {
        case CircuitState::Closed: return "CLOSED";
        case CircuitState::Open: return "OPEN";
        case CircuitState::HalfOpen: return "HALF-OPEN";
        default: return "UNKNOWN";
    }
}

int main() {
    std::cout << "=== Circuit Breaker Example ===\n\n";
    
    // 1. Configure with circuit breaker
    ProcessTransportConfig transport_config;
    transport_config.command = "npx";
    transport_config.args = {"-y", "@modelcontextprotocol/server-filesystem", "/tmp"};
    
    auto transport = std::make_unique<ProcessTransport>(transport_config);
    if (!transport->start()) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    
    // 2. Configure client with circuit breaker
    McpClientConfig client_config;
    client_config.enable_circuit_breaker = true;
    client_config.circuit_breaker.failure_threshold = 3;  // Open after 3 failures
    client_config.circuit_breaker.recovery_timeout = std::chrono::seconds(5);  // Short for demo
    client_config.circuit_breaker.success_threshold = 2;  // Need 2 successes to close
    
    std::cout << "Circuit Breaker Configuration:\n";
    std::cout << "  Failure threshold: " << client_config.circuit_breaker.failure_threshold << "\n";
    std::cout << "  Recovery timeout: 5 seconds\n";
    std::cout << "  Success threshold: " << client_config.circuit_breaker.success_threshold << "\n\n";
    
    McpClient client(std::move(transport), client_config);
    
    // 3. Register state change callback
    client.on_circuit_state_change([](CircuitState old_state, CircuitState new_state) {
        std::cout << "\n*** Circuit state changed: " 
                  << state_to_string(old_state) << " -> " 
                  << state_to_string(new_state) << " ***\n\n";
    });
    
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
    
    // 5. Show initial state
    std::cout << "=== Initial Circuit State ===\n";
    std::cout << "State: " << state_to_string(client.circuit_state()) << "\n";
    std::cout << "Is open: " << (client.is_circuit_open() ? "yes" : "no") << "\n\n";
    
    // 6. Make some successful requests
    std::cout << "=== Making Successful Requests ===\n";
    for (int i = 0; i < 3; ++i) {
        auto result = client.call_tool("list_directory", {{"path", "/tmp"}});
        if (result) {
            std::cout << "Request " << (i+1) << ": SUCCESS\n";
        } else {
            std::cout << "Request " << (i+1) << ": FAILED - " << result.error().message << "\n";
        }
    }
    std::cout << "\n";
    
    // 7. Show stats after successes
    std::cout << "=== Stats After Successes ===\n";
    auto stats = client.circuit_stats();
    std::cout << "Total requests: " << stats.total_requests << "\n";
    std::cout << "Successful: " << stats.successful_requests << "\n";
    std::cout << "Failed: " << stats.failed_requests << "\n";
    std::cout << "Rejected: " << stats.rejected_requests << "\n";
    std::cout << "State: " << state_to_string(client.circuit_state()) << "\n\n";
    
    // 8. Demonstrate manual circuit control
    std::cout << "=== Manual Circuit Control Demo ===\n";
    
    std::cout << "Forcing circuit OPEN...\n";
    client.force_circuit_open();
    std::cout << "State: " << state_to_string(client.circuit_state()) << "\n";
    
    // Try a request while open
    std::cout << "\nAttempting request while circuit is OPEN...\n";
    auto blocked_result = client.call_tool("list_directory", {{"path", "/tmp"}});
    if (!blocked_result) {
        std::cout << "Request rejected: " << blocked_result.error().message << "\n";
    }
    
    // Check stats - should show rejected
    stats = client.circuit_stats();
    std::cout << "Rejected requests: " << stats.rejected_requests << "\n\n";
    
    // Force close
    std::cout << "Forcing circuit CLOSED...\n";
    client.force_circuit_closed();
    std::cout << "State: " << state_to_string(client.circuit_state()) << "\n\n";
    
    // 9. Request should work now
    std::cout << "=== Request After Force Close ===\n";
    auto after_close = client.call_tool("list_directory", {{"path", "/tmp"}});
    if (after_close) {
        std::cout << "Request: SUCCESS\n";
    } else {
        std::cout << "Request: FAILED - " << after_close.error().message << "\n";
    }
    std::cout << "\n";
    
    // 10. Final stats
    std::cout << "=== Final Statistics ===\n";
    stats = client.circuit_stats();
    std::cout << "Total requests: " << stats.total_requests << "\n";
    std::cout << "Successful: " << stats.successful_requests << "\n";
    std::cout << "Failed: " << stats.failed_requests << "\n";
    std::cout << "Rejected: " << stats.rejected_requests << "\n";
    std::cout << "Final state: " << state_to_string(client.circuit_state()) << "\n\n";
    
    // 11. Cleanup
    std::cout << "Disconnecting...\n";
    client.disconnect();
    std::cout << "Done!\n";
    
    return 0;
}

