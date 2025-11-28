#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// Async Process Transport
// ═══════════════════════════════════════════════════════════════════════════
// Spawns a subprocess and communicates via stdin/stdout using ASIO's
// async file descriptors for true non-blocking I/O.
//
// Key features:
// - Non-blocking reads via asio::posix::stream_descriptor
// - Coroutine-native API (no blocking calls)
// - Buffered message channel for backpressure handling
// - Graceful shutdown with SIGTERM -> SIGKILL escalation

// Platform check
#if !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
#error "AsyncProcessTransport is only available on POSIX-compatible systems"
#endif

#include "mcpp/async/async_transport.hpp"

#include <asio/experimental/channel.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <sys/types.h>  // pid_t

namespace mcpp::async {

// ═══════════════════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════════════════

/// How to handle stderr from the subprocess
enum class StderrHandling {
    Discard,     // Redirect to /dev/null
    Passthrough, // Inherit from parent
    Capture      // Capture to buffer (future)
};

struct AsyncProcessConfig {
    std::string command;
    std::vector<std::string> args;
    
    /// Use Content-Length framing (true) or newline-delimited JSON (false)
    bool use_content_length_framing{true};
    
    /// Maximum message size
    std::size_t max_message_size{1 << 20};  // 1 MiB
    
    /// How to handle stderr
    StderrHandling stderr_handling{StderrHandling::Discard};
    
    /// Channel buffer size for received messages
    std::size_t channel_capacity{16};
    
    /// Graceful shutdown timeout before SIGKILL
    std::chrono::milliseconds shutdown_timeout{std::chrono::seconds(5)};
    
    /// Security: Skip command validation (only for trusted/test scenarios)
    /// WARNING: Setting to true allows arbitrary command execution!
    bool skip_command_validation{false};
};

// ═══════════════════════════════════════════════════════════════════════════
// Async Process Transport
// ═══════════════════════════════════════════════════════════════════════════

class AsyncProcessTransport : public IAsyncTransport {
public:
    /// Construct with executor and config
    AsyncProcessTransport(asio::any_io_executor executor, AsyncProcessConfig config);
    ~AsyncProcessTransport() override;

    // Non-copyable, non-movable
    AsyncProcessTransport(const AsyncProcessTransport&) = delete;
    AsyncProcessTransport& operator=(const AsyncProcessTransport&) = delete;
    AsyncProcessTransport(AsyncProcessTransport&&) = delete;
    AsyncProcessTransport& operator=(AsyncProcessTransport&&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // IAsyncTransport interface
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] asio::any_io_executor get_executor() override;
    [[nodiscard]] asio::awaitable<TransportResult<void>> async_start() override;
    [[nodiscard]] asio::awaitable<void> async_stop() override;
    [[nodiscard]] asio::awaitable<TransportResult<void>> async_send(Json message) override;
    [[nodiscard]] asio::awaitable<TransportResult<Json>> async_receive() override;
    [[nodiscard]] bool is_running() const override;

    // ─────────────────────────────────────────────────────────────────────────
    // Process-specific methods
    // ─────────────────────────────────────────────────────────────────────────

    /// Get child process PID (-1 if not running)
    [[nodiscard]] pid_t child_pid() const;

    /// Check if child process is still alive
    [[nodiscard]] bool is_child_alive() const;

    /// Get exit code (only valid after process exits)
    [[nodiscard]] std::optional<int> exit_code() const;

    /// Get captured stderr output (only valid if stderr_handling == Capture)
    [[nodiscard]] std::string get_stderr() const;

private:
    // Internal coroutines
    asio::awaitable<void> reader_loop();
    asio::awaitable<void> stderr_reader_loop();
    asio::awaitable<TransportResult<Json>> read_framed_message();
    asio::awaitable<TransportResult<Json>> read_line_message();
    asio::awaitable<std::string> read_until(char delimiter, std::size_t max_size);
    asio::awaitable<std::string> read_exactly(std::size_t count);
    
    // Process management
    TransportResult<void> spawn_process();
    void terminate_process();
    void check_child_status();

    // Configuration
    AsyncProcessConfig config_;

    // Executor and strand for serialization
    asio::any_io_executor executor_;
    asio::strand<asio::any_io_executor> strand_;

    // ASIO stream descriptors for async I/O
    std::unique_ptr<asio::posix::stream_descriptor> stdin_stream_;
    std::unique_ptr<asio::posix::stream_descriptor> stdout_stream_;
    std::unique_ptr<asio::posix::stream_descriptor> stderr_stream_;

    // Message channel (producer: reader_loop, consumer: async_receive)
    using MessageChannel = asio::experimental::channel<
        void(asio::error_code, TransportResult<Json>)
    >;
    std::unique_ptr<MessageChannel> message_channel_;

    // Process state
    pid_t child_pid_{-1};
    std::atomic<bool> running_{false};
    std::optional<int> exit_code_;

    // Read buffer for efficiency
    std::string read_buffer_;

    // Stderr capture (only used when stderr_handling == Capture)
    mutable std::mutex stderr_mutex_;
    std::string stderr_buffer_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════════════════════

inline std::unique_ptr<IAsyncTransport> make_async_process_transport(
    asio::any_io_executor executor,
    AsyncProcessConfig config
) {
    return std::make_unique<AsyncProcessTransport>(std::move(executor), std::move(config));
}

}  // namespace mcpp::async


