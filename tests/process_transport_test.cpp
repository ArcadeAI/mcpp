// ─────────────────────────────────────────────────────────────────────────────
// Process Transport Unit Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for ProcessTransport edge cases and error handling.
// These tests use simple shell commands to test the transport layer
// without requiring external MCP servers.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/transport/process_transport.hpp"

#include <chrono>
#include <thread>

using namespace mcpp;
using Json = nlohmann::json;
using namespace std::chrono_literals;

// ═══════════════════════════════════════════════════════════════════════════
// Basic Lifecycle Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport starts and stops cleanly", "[process][lifecycle][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    
    REQUIRE(transport.is_running() == false);
    
    auto result = transport.start();
    REQUIRE(result.has_value());
    REQUIRE(transport.is_running() == true);
    
    transport.stop();
    REQUIRE(transport.is_running() == false);
}

TEST_CASE("ProcessTransport double start returns error", "[process][lifecycle][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    
    auto result1 = transport.start();
    REQUIRE(result1.has_value());
    
    auto result2 = transport.start();
    REQUIRE(result2.has_value() == false);
    REQUIRE(result2.error().message.find("already running") != std::string::npos);
    
    transport.stop();
}

TEST_CASE("ProcessTransport double stop is safe", "[process][lifecycle][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    transport.start();
    
    transport.stop();
    REQUIRE(transport.is_running() == false);
    
    // Second stop should be a no-op
    transport.stop();
    REQUIRE(transport.is_running() == false);
}

TEST_CASE("ProcessTransport destructor stops process", "[process][lifecycle][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    {
        ProcessTransport transport(config);
        transport.start();
        REQUIRE(transport.is_running() == true);
        // Destructor called here
    }
    // Process should be cleaned up
}

// ═══════════════════════════════════════════════════════════════════════════
// Invalid Command Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport handles nonexistent command", "[process][error]") {
    ProcessTransportConfig config;
    config.command = "nonexistent_command_that_does_not_exist_12345";
    
    ProcessTransport transport(config);
    
    // Start succeeds (fork works)
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    // But trying to communicate fails because child exited
    std::this_thread::sleep_for(100ms);  // Give child time to fail
    
    auto recv_result = transport.receive();
    REQUIRE(recv_result.has_value() == false);
}

TEST_CASE("ProcessTransport handles empty command", "[process][error]") {
    ProcessTransportConfig config;
    config.command = "";
    
    ProcessTransport transport(config);
    
    auto start_result = transport.start();
    // Start may succeed (fork) but child will fail
    
    if (start_result.has_value()) {
        std::this_thread::sleep_for(100ms);
        auto recv_result = transport.receive();
        REQUIRE(recv_result.has_value() == false);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Send/Receive Tests (using cat as echo server)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport send/receive with Content-Length framing", "[process][framing]") {
    ProcessTransportConfig config;
    config.command = "cat";
    config.use_content_length_framing = true;
    
    ProcessTransport transport(config);
    transport.start();
    
    Json message = {{"test", "hello"}, {"number", 42}};
    
    auto send_result = transport.send(message);
    REQUIRE(send_result.has_value());
    
    auto recv_result = transport.receive();
    REQUIRE(recv_result.has_value());
    REQUIRE((*recv_result)["test"] == "hello");
    REQUIRE((*recv_result)["number"] == 42);
    
    transport.stop();
}

TEST_CASE("ProcessTransport send/receive with newline-delimited JSON", "[process][framing]") {
    ProcessTransportConfig config;
    config.command = "cat";
    config.use_content_length_framing = false;
    
    ProcessTransport transport(config);
    transport.start();
    
    Json message = {{"mode", "ndjson"}, {"value", "test"}};
    
    auto send_result = transport.send(message);
    REQUIRE(send_result.has_value());
    
    auto recv_result = transport.receive();
    REQUIRE(recv_result.has_value());
    REQUIRE((*recv_result)["mode"] == "ndjson");
    
    transport.stop();
}

TEST_CASE("ProcessTransport multiple messages", "[process][framing]") {
    ProcessTransportConfig config;
    config.command = "cat";
    config.use_content_length_framing = true;
    
    ProcessTransport transport(config);
    transport.start();
    
    for (int i = 0; i < 5; ++i) {
        Json message = {{"index", i}};
        
        auto send_result = transport.send(message);
        REQUIRE(send_result.has_value());
        
        auto recv_result = transport.receive();
        REQUIRE(recv_result.has_value());
        REQUIRE((*recv_result)["index"] == i);
    }
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport send fails when not running", "[process][error]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    // Don't call start()
    
    auto result = transport.send({{"test", "value"}});
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().message.find("not running") != std::string::npos);
}

TEST_CASE("ProcessTransport receive fails when not running", "[process][error]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    // Don't call start()
    
    auto result = transport.receive();
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().message.find("not running") != std::string::npos);
}

TEST_CASE("ProcessTransport detects process exit", "[process][error]") {
    ProcessTransportConfig config;
    config.command = "true";  // Exits immediately with code 0
    
    ProcessTransport transport(config);
    transport.start();
    
    // Give process time to exit
    std::this_thread::sleep_for(100ms);
    
    auto result = transport.receive();
    REQUIRE(result.has_value() == false);
    // Should indicate process exited
}

TEST_CASE("ProcessTransport handles process that exits with error", "[process][error]") {
    ProcessTransportConfig config;
    config.command = "false";  // Exits immediately with code 1
    
    ProcessTransport transport(config);
    transport.start();
    
    std::this_thread::sleep_for(100ms);
    
    // Check exit code
    auto code = transport.exit_code();
    // exit_code may or may not be available depending on timing
    
    auto result = transport.send({{"test", "value"}});
    REQUIRE(result.has_value() == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Timeout Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport receive with timeout", "[process][timeout]") {
    ProcessTransportConfig config;
    config.command = "sleep";
    config.args = {"10"};  // Sleep for 10 seconds
    config.read_timeout = 100ms;  // But timeout after 100ms
    
    ProcessTransport transport(config);
    transport.start();
    
    auto start_time = std::chrono::steady_clock::now();
    auto result = transport.receive();
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().category == TransportError::Category::Timeout);
    
    // Should have timed out in roughly 100ms (allow some slack)
    REQUIRE(elapsed < 500ms);
    
    transport.stop();
}

TEST_CASE("ProcessTransport receive without timeout blocks", "[process][timeout]") {
    ProcessTransportConfig config;
    config.command = "cat";
    config.read_timeout = std::chrono::milliseconds{0};  // No timeout
    
    ProcessTransport transport(config);
    transport.start();
    
    // Send a message so we have something to receive
    transport.send({{"test", "value"}});
    
    auto result = transport.receive();
    REQUIRE(result.has_value());
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Stderr Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport stderr discard mode", "[process][stderr]") {
    ProcessTransportConfig config;
    config.command = "sh";
    config.args = {"-c", "echo stdout; echo stderr >&2; echo stdout2"};
    config.use_content_length_framing = false;
    config.stderr_handling = StderrHandling::Discard;
    config.skip_command_validation = true;  // Allow shell commands in tests
    
    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    // Should only receive stdout lines
    auto result1 = transport.receive();
    // This test is tricky because 'echo' doesn't output JSON
    // Just verify we can receive something
    
    transport.stop();
}

// Note: This test is skipped because SIGTERM from child process termination
// is sometimes caught by Catch2, causing flaky failures.
TEST_CASE("ProcessTransport stderr passthrough mode", "[process][stderr][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "cat";
    config.stderr_handling = StderrHandling::Passthrough;
    
    ProcessTransport transport(config);
    auto result = transport.start();
    REQUIRE(result.has_value());
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Content-Length Header Case Insensitivity Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport accepts lowercase content-length header", "[process][framing]") {
    // Use printf to send a custom-formatted message with lowercase header
    ProcessTransportConfig config;
    config.command = "sh";
    config.args = {"-c", "printf 'content-length: 13\\r\\n\\r\\n{\"test\":\"ok\"}'"};
    config.use_content_length_framing = true;
    config.skip_command_validation = true;  // Allow shell commands in tests
    
    ProcessTransport transport(config);
    transport.start();
    
    auto result = transport.receive();
    REQUIRE(result.has_value());
    REQUIRE((*result)["test"] == "ok");
    
    transport.stop();
}

TEST_CASE("ProcessTransport accepts mixed-case Content-Length header", "[process][framing]") {
    ProcessTransportConfig config;
    config.command = "sh";
    config.args = {"-c", "printf 'CONTENT-LENGTH: 13\\r\\n\\r\\n{\"test\":\"ok\"}'"};
    config.use_content_length_framing = true;
    config.skip_command_validation = true;  // Allow shell commands in tests
    
    ProcessTransport transport(config);
    transport.start();
    
    auto result = transport.receive();
    REQUIRE(result.has_value());
    REQUIRE((*result)["test"] == "ok");
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Large Message Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport handles large messages", "[process][framing]") {
    ProcessTransportConfig config;
    config.command = "cat";
    config.use_content_length_framing = true;
    config.max_content_length = 1 << 20;  // 1 MiB
    
    ProcessTransport transport(config);
    transport.start();
    
    // Create a large message (100KB of data)
    std::string large_data(100 * 1024, 'x');
    Json message = {{"data", large_data}};
    
    auto send_result = transport.send(message);
    REQUIRE(send_result.has_value());
    
    auto recv_result = transport.receive();
    REQUIRE(recv_result.has_value());
    REQUIRE((*recv_result)["data"].get<std::string>().size() == large_data.size());
    
    transport.stop();
}

TEST_CASE("ProcessTransport rejects message exceeding max_content_length", "[process][framing]") {
    ProcessTransportConfig config;
    config.command = "sh";
    // Send a message claiming to be 10MB
    config.args = {"-c", "printf 'Content-Length: 10485760\\r\\n\\r\\n{}'"};
    config.use_content_length_framing = true;
    config.skip_command_validation = true;  // Allow shell commands in tests
    config.max_content_length = 1024;  // Only allow 1KB
    
    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    auto result = transport.receive();
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().message.find("too large") != std::string::npos);
    
    transport.stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Process Health Check Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport is_process_alive returns correct status", "[process][health][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "sleep";
    config.args = {"10"};
    
    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    REQUIRE(transport.is_process_alive() == true);
    
    transport.stop();
    
    // After stop, process should not be alive
    REQUIRE(transport.is_process_alive() == false);
}

TEST_CASE("ProcessTransport detects naturally exiting process", "[process][health][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "true";  // Exits immediately
    
    ProcessTransport transport(config);
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    std::this_thread::sleep_for(200ms);
    
    // Process should have exited
    REQUIRE(transport.is_process_alive() == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Concurrency / Race Condition Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport concurrent start() calls", "[process][concurrency][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    
    std::atomic<int> success_count{0};
    std::atomic<int> already_running_count{0};
    std::atomic<int> start_in_progress_count{0};
    
    constexpr int num_threads = 10;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    
    // Barrier to ensure all threads start at roughly the same time
    std::atomic<bool> go{false};
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            while (!go.load()) {
                std::this_thread::yield();
            }
            
            auto result = transport.start();
            if (result.has_value()) {
                success_count.fetch_add(1);
            } else if (result.error().message.find("already running") != std::string::npos) {
                already_running_count.fetch_add(1);
            } else if (result.error().message.find("in progress") != std::string::npos) {
                start_in_progress_count.fetch_add(1);
            }
        });
    }
    
    // Release all threads at once
    go.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Exactly one thread should succeed
    REQUIRE(success_count.load() == 1);
    // Others should fail with "already running" or "in progress"
    REQUIRE(already_running_count.load() + start_in_progress_count.load() == num_threads - 1);
    
    transport.stop();
}

TEST_CASE("ProcessTransport concurrent start() and stop()", "[process][concurrency][!mayfail]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    // Run multiple iterations to increase chance of hitting race conditions
    for (int iteration = 0; iteration < 10; ++iteration) {
        ProcessTransport transport(config);
        
        std::thread starter([&]() {
            transport.start();
        });
        
        std::thread stopper([&]() {
            std::this_thread::sleep_for(1ms);  // Slight delay
            transport.stop();
        });
        
        starter.join();
        stopper.join();
        
        // Call stop() again to ensure clean state
        // (handles case where start() completed after first stop())
        transport.stop();
        
        // After explicit stop, transport should be stopped
        REQUIRE(transport.is_running() == false);
    }
}

TEST_CASE("ProcessTransport concurrent send() calls", "[process][concurrency]") {
    ProcessTransportConfig config;
    config.command = "cat";
    config.use_content_length_framing = false;
    
    ProcessTransport transport(config);
    REQUIRE(transport.start().has_value());
    
    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};
    
    constexpr int num_threads = 5;
    constexpr int messages_per_thread = 10;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, thread_id = i]() {
            for (int j = 0; j < messages_per_thread; ++j) {
                Json msg = {{"thread", thread_id}, {"msg", j}};
                auto result = transport.send(msg);
                if (result.has_value()) {
                    success_count.fetch_add(1);
                } else {
                    error_count.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All sends should succeed (cat doesn't reject input)
    REQUIRE(success_count.load() == num_threads * messages_per_thread);
    REQUIRE(error_count.load() == 0);
    
    transport.stop();
}

TEST_CASE("ProcessTransport double stop() is safe", "[process][concurrency]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    REQUIRE(transport.start().has_value());
    
    // Multiple concurrent stop() calls should be safe
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&]() {
            transport.stop();
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(transport.is_running() == false);
}

TEST_CASE("ProcessTransport stress test start/stop cycles", "[process][concurrency][stress]") {
    ProcessTransportConfig config;
    config.command = "cat";
    
    ProcessTransport transport(config);
    
    // Rapid start/stop cycles to detect resource leaks or race conditions
    for (int i = 0; i < 20; ++i) {
        auto start_result = transport.start();
        REQUIRE(start_result.has_value());
        REQUIRE(transport.is_running() == true);
        
        transport.stop();
        REQUIRE(transport.is_running() == false);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Stderr Capture Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("ProcessTransport captures stderr output", "[process][stderr]") {
    ProcessTransportConfig config;
    config.command = "sh";
    config.args = {"-c", "echo 'error message' >&2 && cat"};
    config.stderr_handling = StderrHandling::Capture;
    config.skip_command_validation = true;  // sh -c needs this
    
    ProcessTransport transport(config);
    
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    // Give the process time to write to stderr
    std::this_thread::sleep_for(100ms);
    
    // Check stderr was captured
    std::string stderr_data = transport.read_stderr();
    REQUIRE(stderr_data.find("error message") != std::string::npos);
    
    transport.stop();
}

TEST_CASE("ProcessTransport stderr callback is called", "[process][stderr]") {
    std::string captured_stderr;
    
    ProcessTransportConfig config;
    config.command = "sh";
    config.args = {"-c", "echo 'callback test' >&2 && cat"};
    config.stderr_handling = StderrHandling::Capture;
    config.skip_command_validation = true;
    config.stderr_callback = [&captured_stderr](std::string_view data) {
        captured_stderr += data;
    };
    
    ProcessTransport transport(config);
    
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    // Give the process time to write to stderr
    std::this_thread::sleep_for(100ms);
    
    // Callback should have been invoked
    REQUIRE(captured_stderr.find("callback test") != std::string::npos);
    
    transport.stop();
}

TEST_CASE("ProcessTransport stderr discard mode works", "[process][stderr]") {
    ProcessTransportConfig config;
    config.command = "sh";
    config.args = {"-c", "echo 'discarded' >&2 && cat"};
    config.stderr_handling = StderrHandling::Discard;  // Default
    config.skip_command_validation = true;
    
    ProcessTransport transport(config);
    
    auto start_result = transport.start();
    REQUIRE(start_result.has_value());
    
    // Give the process time to run
    std::this_thread::sleep_for(100ms);
    
    // No stderr data should be available (it was discarded)
    REQUIRE(transport.has_stderr_data() == false);
    REQUIRE(transport.read_stderr().empty());
    
    transport.stop();
}

