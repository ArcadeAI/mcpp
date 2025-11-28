// ─────────────────────────────────────────────────────────────────────────────
// AsyncProcessTransport Stderr Capture Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for stderr capture functionality in AsyncProcessTransport

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/async/async_process_transport.hpp"
#include "mcpp/log/logger.hpp"

#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

#include <chrono>
#include <future>
#include <thread>

using namespace mcpp::async;
using Json = nlohmann::json;
using namespace std::chrono_literals;

// Helper to run async operations
template <typename T>
T run_sync(asio::io_context& io, asio::awaitable<T>&& coro) {
    std::promise<T> promise;
    auto future = promise.get_future();

    asio::co_spawn(io, [&]() -> asio::awaitable<T> {
        try {
            T result = co_await std::move(coro);
            promise.set_value(std::move(result));
            co_return result;
        } catch (...) {
            promise.set_exception(std::current_exception());
            throw;
        }
    }, asio::detached);

    io.run();
    io.restart();

    return future.get();
}

// Specialization for void
template <>
void run_sync(asio::io_context& io, asio::awaitable<void>&& coro) {
    std::promise<void> promise;
    auto future = promise.get_future();

    asio::co_spawn(io, [&]() -> asio::awaitable<void> {
        try {
            co_await std::move(coro);
            promise.set_value();
        } catch (...) {
            promise.set_exception(std::current_exception());
            throw;
        }
    }, asio::detached);

    io.run();
    io.restart();

    future.get();
}

// ═══════════════════════════════════════════════════════════════════════════
// Stderr Capture Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AsyncProcessTransport captures stderr when Capture enabled", "[async][process][stderr]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "sh";
    config.args = {"-c", "echo 'stdout message' && echo 'stderr message' >&2"};
    config.stderr_handling = StderrHandling::Capture;
    config.use_content_length_framing = false;  // Use newline-delimited for simplicity
    config.skip_command_validation = true;  // Allow shell commands in tests
    
    AsyncProcessTransport transport(io.get_executor(), std::move(config));
    
    auto start_result = run_sync(io, transport.async_start());
    REQUIRE(start_result.has_value());
    
    // Give process time to start
    std::this_thread::sleep_for(50ms);
    
    // Give process time to write to stderr
    std::this_thread::sleep_for(100ms);
    
    // Check that stderr was captured
    std::string stderr_output = transport.get_stderr();
    REQUIRE(stderr_output.find("stderr message") != std::string::npos);
    
    // Cleanup
    run_sync(io, transport.async_stop());
}

TEST_CASE("AsyncProcessTransport does not capture stderr when Discard enabled", "[async][process][stderr]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "sh";
    config.args = {"-c", "echo 'stderr message' >&2"};
    config.stderr_handling = StderrHandling::Discard;
    config.skip_command_validation = true;  // Allow shell commands in tests
    
    AsyncProcessTransport transport(io.get_executor(), std::move(config));
    
    auto start_result = run_sync(io, transport.async_start());
    REQUIRE(start_result.has_value());
    
    // Give process time to start
    std::this_thread::sleep_for(50ms);
    
    // Give process time to write to stderr
    std::this_thread::sleep_for(100ms);
    
    // Check that stderr was NOT captured
    std::string stderr_output = transport.get_stderr();
    REQUIRE(stderr_output.empty());
    
    // Cleanup
    run_sync(io, transport.async_stop());
}

TEST_CASE("AsyncProcessTransport accumulates stderr output", "[async][process][stderr]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "sh";
    config.args = {"-c", "echo 'line1' >&2 && sleep 0.1 && echo 'line2' >&2"};
    config.stderr_handling = StderrHandling::Capture;
    config.skip_command_validation = true;  // Allow shell commands in tests
    
    AsyncProcessTransport transport(io.get_executor(), std::move(config));
    
    auto start_result = run_sync(io, transport.async_start());
    REQUIRE(start_result.has_value());
    
    // Give process time to start
    std::this_thread::sleep_for(50ms);
    
    // Give process time to write both lines
    std::this_thread::sleep_for(200ms);
    
    // Check that both lines were captured
    std::string stderr_output = transport.get_stderr();
    REQUIRE(stderr_output.find("line1") != std::string::npos);
    REQUIRE(stderr_output.find("line2") != std::string::npos);
    
    // Cleanup
    run_sync(io, transport.async_stop());
}

TEST_CASE("AsyncProcessTransport get_stderr returns empty when Capture not enabled", "[async][process][stderr]") {
    asio::io_context io;
    
    AsyncProcessConfig config;
    config.command = "sh";
    config.args = {"-c", "echo 'test' >&2"};
    config.stderr_handling = StderrHandling::Passthrough;
    config.skip_command_validation = true;  // Allow shell commands in tests
    
    AsyncProcessTransport transport(io.get_executor(), std::move(config));
    
    auto start_result = run_sync(io, transport.async_start());
    REQUIRE(start_result.has_value());
    
    // Give process time to start
    std::this_thread::sleep_for(50ms);
    
    // get_stderr should return empty when not capturing
    std::string stderr_output = transport.get_stderr();
    REQUIRE(stderr_output.empty());
    
    // Cleanup
    run_sync(io, transport.async_stop());
}

