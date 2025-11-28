# Example 06: Circuit Breaker

Resilience pattern to prevent cascading failures when servers are unavailable.

## Circuit Breaker States

```
                    ┌──────────────────────────────────────────────────┐
                    │                                                  │
                    │    ┌────────┐   failure_threshold   ┌────────┐  │
                    │    │        │   reached             │        │  │
         success    │    │ CLOSED ├──────────────────────►│  OPEN  │  │
         ◄──────────┴────┤        │                       │        │  │
                         │ Allow  │                       │ Reject │  │
                         │  all   │                       │  all   │  │
                         │requests│                       │requests│  │
                         └────────┘                       └───┬────┘  │
                              ▲                               │       │
                              │                               │       │
                              │     success                   │       │
                              │     ┌────────────┐            │       │
                              │     │            │            │       │
                              └─────┤ HALF-OPEN  │◄───────────┘       │
                                    │            │  recovery_timeout  │
                         failure    │ Allow one  │                    │
                         ───────────┤ test req   ├────────────────────┘
                                    └────────────┘
```

## Configuration

```cpp
CircuitBreakerConfig config;

// How many failures before opening circuit
config.failure_threshold = 5;

// How long to wait before trying again
config.recovery_timeout = std::chrono::seconds(30);

// How many successes needed to close circuit
config.success_threshold = 2;
```

## How It Works

1. **CLOSED** (normal): All requests pass through
2. **Failures accumulate**: Each failure increments counter
3. **Threshold reached**: Circuit opens after N consecutive failures
4. **OPEN**: All requests immediately rejected (fast-fail)
5. **Recovery timeout**: After waiting, circuit enters HALF-OPEN
6. **HALF-OPEN**: One test request allowed
7. **Test succeeds**: Circuit closes, back to normal
8. **Test fails**: Circuit reopens, wait again

## Benefits

- **Fast failure**: Don't wait for timeouts on dead servers
- **Resource protection**: Don't overwhelm struggling servers
- **Recovery**: Automatic retry after cooldown
- **Visibility**: Know when services are unhealthy

## Code Example

```cpp
McpClientConfig config;
config.enable_circuit_breaker = true;
config.circuit_breaker.failure_threshold = 5;
config.circuit_breaker.recovery_timeout = std::chrono::seconds(30);
config.circuit_breaker.success_threshold = 2;

McpClient client(std::move(transport), config);

// Register for state changes
client.on_circuit_state_change([](CircuitState old_state, CircuitState new_state) {
    std::cout << "Circuit: " << to_string(old_state) 
              << " -> " << to_string(new_state) << "\n";
});

// Check state before operations
if (client.is_circuit_open()) {
    std::cout << "Server unavailable, circuit is open\n";
    // Maybe use cached data or fallback
} else {
    auto result = client.call_tool("my_tool", args);
    // ...
}

// Get statistics
auto stats = client.circuit_stats();
std::cout << "Total: " << stats.total_requests << "\n";
std::cout << "Success: " << stats.successful_requests << "\n";
std::cout << "Failed: " << stats.failed_requests << "\n";
std::cout << "Rejected: " << stats.rejected_requests << "\n";

// Manual control (for testing/maintenance)
client.force_circuit_open();   // Manually open
client.force_circuit_closed(); // Manually close
```

## When to Use

- **Remote servers**: HTTP transport to external services
- **Unreliable networks**: Mobile, edge deployments
- **Critical paths**: Where fast failure is better than slow timeout
- **Load protection**: Prevent thundering herd on recovery

## Running

```bash
./circuit_breaker_example
```

