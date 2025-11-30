#pragma once

// Platform check - ProcessTransport requires POSIX APIs
#if !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)
#error "ProcessTransport is only available on POSIX-compatible systems (Linux, macOS, BSD)"
#endif

#include "mcpp/transport.hpp"

#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <functional>
#include <thread>
#include <vector>

#include <sys/types.h>  // For pid_t

namespace mcpp {

using Json = nlohmann::json;

// ═══════════════════════════════════════════════════════════════════════════
// Process Transport Configuration
// ═══════════════════════════════════════════════════════════════════════════

/// How to handle stderr from the subprocess
enum class StderrHandling {
    Discard,     // Redirect to /dev/null (default)
    Passthrough, // Let stderr go to parent's stderr
    Capture      // Capture stderr (accessible via read_stderr())
};

/// Callback type for stderr data
using StderrCallback = std::function<void(std::string_view)>;

struct ProcessTransportConfig {
    std::string command;                     // Command to execute
    std::vector<std::string> args;           // Command arguments
    std::size_t max_content_length{1 << 20}; // 1 MiB default
    bool use_content_length_framing{true};   // Use Content-Length headers (false = raw JSON per line)
    StderrHandling stderr_handling{StderrHandling::Discard};
    std::chrono::milliseconds read_timeout{0}; // 0 = no timeout (blocking)
    
    /// Callback for stderr data (only used when stderr_handling == Capture)
    StderrCallback stderr_callback;
    
    // Security: Skip command validation (only for trusted/test scenarios)
    // WARNING: Setting to true allows arbitrary command execution!
    bool skip_command_validation{false};
};

// ═══════════════════════════════════════════════════════════════════════════
// Process Transport
// ═══════════════════════════════════════════════════════════════════════════
// Spawns a subprocess and communicates via stdin/stdout using JSON-RPC framing.
//
// This is useful for testing against real MCP servers that use stdio transport.

class ProcessTransport {
public:
    explicit ProcessTransport(ProcessTransportConfig config);
    ~ProcessTransport();

    // Non-copyable, non-movable
    ProcessTransport(const ProcessTransport&) = delete;
    ProcessTransport& operator=(const ProcessTransport&) = delete;
    ProcessTransport(ProcessTransport&&) = delete;
    ProcessTransport& operator=(ProcessTransport&&) = delete;

    /// Start the subprocess
    [[nodiscard]] TransportResult<void> start();

    /// Stop the subprocess
    void stop();

    /// Check if the subprocess is running
    [[nodiscard]] bool is_running() const;

    /// Send a JSON message to the subprocess
    [[nodiscard]] TransportResult<void> send(const Json& message);

    /// Receive a JSON message from the subprocess
    [[nodiscard]] TransportResult<Json> receive();

    /// Check if the child process is still alive
    [[nodiscard]] bool is_process_alive() const;

    /// Get the child process exit code (only valid after process exits)
    [[nodiscard]] std::optional<int> exit_code() const;
    
    /// Read captured stderr data (only available when stderr_handling == Capture)
    /// Returns empty string if no data or not capturing
    [[nodiscard]] std::string read_stderr();
    
    /// Check if there's stderr data available
    [[nodiscard]] bool has_stderr_data() const;

private:
    void stderr_reader_loop();
    [[nodiscard]] TransportResult<Json> receive_line();
    [[nodiscard]] TransportResult<Json> receive_framed();
    /// Wait for fd to be readable. Called with mutex held, fd passed explicitly.
    [[nodiscard]] static bool wait_for_readable(int fd, std::chrono::milliseconds timeout);
    void check_process_status();
    
    /// Fill read buffer from stdout_fd_. Returns bytes read or error.
    [[nodiscard]] TransportResult<std::size_t> fill_read_buffer();
    
    /// Read exactly n bytes into dest. Returns error if EOF or failure.
    [[nodiscard]] TransportResult<void> read_exact(char* dest, std::size_t n);

    ProcessTransportConfig config_;
    int stdin_fd_{-1};
    int stdout_fd_{-1};
    pid_t child_pid_{-1};
    bool running_{false};
    bool starting_{false};  // Prevents concurrent start() calls
    std::optional<int> exit_code_;
    mutable std::mutex mutex_;
    
    // Read buffer to avoid syscall-per-byte
    static constexpr std::size_t read_buffer_size = 8192;
    char read_buffer_[read_buffer_size];
    std::size_t read_buffer_pos_{0};
    std::size_t read_buffer_len_{0};
    
    // Stderr capture
    int stderr_fd_{-1};
    std::thread stderr_thread_;
    std::string stderr_buffer_;
    mutable std::mutex stderr_mutex_;
};

}  // namespace mcpp


