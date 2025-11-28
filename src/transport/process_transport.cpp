#include "mcpp/transport/process_transport.hpp"
#include "mcpp/log/logger.hpp"

#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace mcpp {

namespace {

// Named constants for magic numbers
constexpr useconds_t kProcessTerminationWaitUs = 100'000;  // 100ms wait before SIGKILL

TransportError make_error(TransportError::Category cat, const std::string& msg) {
    return TransportError{cat, msg, std::nullopt};
}

}  // namespace

ProcessTransport::ProcessTransport(ProcessTransportConfig config)
    : config_(std::move(config))
{}

ProcessTransport::~ProcessTransport() {
    stop();
}

// Platform-specific allowed command prefixes
#if defined(__APPLE__)
// macOS paths
const std::vector<std::string> kAllowedCommandPrefixes = {
    "/usr/bin/", "/usr/local/bin/", "/bin/", "/opt/homebrew/bin/",
    "/usr/sbin/", "/sbin/", "/Applications/"
};
#elif defined(__linux__)
// Linux paths (including snap and flatpak)
const std::vector<std::string> kAllowedCommandPrefixes = {
    "/usr/bin/", "/usr/local/bin/", "/bin/", "/usr/sbin/", "/sbin/",
    "/snap/bin/", "/var/lib/flatpak/", "/home/", "~/.local/bin/"
};
#elif defined(_WIN32)
// Windows paths
const std::vector<std::string> kAllowedCommandPrefixes = {
    "C:\\Windows\\System32\\", "C:\\Windows\\", "C:\\Program Files\\",
    "C:\\Program Files (x86)\\", "C:\\Users\\"
};
#else
// Fallback for unknown platforms - be restrictive
const std::vector<std::string> kAllowedCommandPrefixes = {
    "/usr/bin/", "/usr/local/bin/", "/bin/"
};
#endif

// Helper to validate command for security
static bool is_safe_command(const std::string& command, const std::vector<std::string>& args) {
    // Reject empty commands
    if (command.empty()) {
        return false;
    }
    
    // Reject shell metacharacters in command that could enable injection
    const std::string dangerous_chars = ";|&$`\\\"'<>(){}[]!#~";
    for (char c : command) {
        if (dangerous_chars.find(c) != std::string::npos) {
            return false;
        }
    }
    
    // Check args for dangerous characters
    for (const auto& arg : args) {
        for (char c : arg) {
            if (dangerous_chars.find(c) != std::string::npos) {
                return false;
            }
        }
    }
    
    // Reject absolute paths outside of expected locations (defense in depth)
    // Allow relative commands (will be resolved via PATH)
#if defined(_WIN32)
    const bool is_absolute = (command.size() >= 2 && command[1] == ':');
#else
    const bool is_absolute = (!command.empty() && command[0] == '/');
#endif
    
    if (is_absolute) {
        bool allowed = false;
        for (const auto& prefix : kAllowedCommandPrefixes) {
            if (command.rfind(prefix, 0) == 0) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            return false;
        }
    }
    
    return true;
}

TransportResult<void> ProcessTransport::start() {
    // Validate command for security (unless explicitly skipped for testing)
    if (!config_.skip_command_validation && !is_safe_command(config_.command, config_.args)) {
        return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Command validation failed: potentially unsafe command or arguments"
        ));
    }
    
    // Quick check-and-claim under lock
    {
        std::lock_guard lock(mutex_);
        if (running_) {
            return tl::unexpected(make_error(
                TransportError::Category::Protocol,
                "Process already running"
            ));
        }
        if (starting_) {
            return tl::unexpected(make_error(
                TransportError::Category::Protocol,
                "Process start already in progress"
            ));
        }
        starting_ = true;  // Claim "starting" state to prevent concurrent start() calls
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Expensive operations OUTSIDE the lock
    // ─────────────────────────────────────────────────────────────────────────

    // CRITICAL: Pre-allocate argv BEFORE fork() to avoid malloc deadlock.
    // After fork(), only the calling thread exists in the child. If another
    // thread held the malloc mutex when fork() occurred, any allocation in
    // the child will deadlock forever.
    //
    // We store copies of strings to avoid const_cast (execvp takes char*const[])
    // The strings must outlive the fork/exec, so we keep them in a vector.
    std::vector<std::string> argv_storage;
    argv_storage.reserve(config_.args.size() + 1);
    argv_storage.push_back(config_.command);
    for (const auto& arg : config_.args) {
        argv_storage.push_back(arg);
    }
    
    // Build char* array pointing to our storage
    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& str : argv_storage) {
        argv.push_back(str.data());
    }
    argv.push_back(nullptr);

    // Create pipes for stdin and stdout
    int stdin_pipe[2];   // [read, write] - we write to stdin_pipe[1]
    int stdout_pipe[2];  // [read, write] - we read from stdout_pipe[0]

    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
        std::lock_guard lock(mutex_);
        starting_ = false;
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Failed to create pipes: " + std::string(strerror(errno))
        ));
    }

    pid_t pid = fork();

    if (pid == -1) {
        // Fork failed - cleanup pipes
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        {
            std::lock_guard lock(mutex_);
            starting_ = false;
        }
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Failed to fork: " + std::string(strerror(errno))
        ));
    }

    if (pid == 0) {
        // Child process - NO ALLOCATIONS ALLOWED (malloc deadlock risk)
        // Redirect stdin
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        // Redirect stdout
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        // Handle stderr based on configuration
        switch (config_.stderr_handling) {
            case StderrHandling::Discard: {
                int devnull = open("/dev/null", O_WRONLY);
                if (devnull != -1) {
                    dup2(devnull, STDERR_FILENO);
                    close(devnull);
                }
                break;
            }
            case StderrHandling::Passthrough:
                // Leave stderr as-is (inherited from parent)
                break;
            case StderrHandling::Capture:
                // TODO: Create pipe for stderr capture
                // For now, treat as passthrough
                break;
        }

        // Execute (argv was pre-allocated before fork)
        execvp(config_.command.c_str(), argv.data());

        // If execvp returns, it failed
        _exit(127);
    }

    // Parent process - close unused pipe ends
    close(stdin_pipe[0]);   // Close read end of stdin pipe
    close(stdout_pipe[1]);  // Close write end of stdout pipe

    // ─────────────────────────────────────────────────────────────────────────
    // Commit results under lock
    // ─────────────────────────────────────────────────────────────────────────
    {
        std::lock_guard lock(mutex_);
        child_pid_ = pid;
        stdin_fd_ = stdin_pipe[1];
        stdout_fd_ = stdout_pipe[0];
        running_ = true;
        starting_ = false;  // Done starting
    }

    MCPP_LOG_INFO("Started process: " + config_.command);

    return {};
}

void ProcessTransport::stop() {
    pid_t pid_to_terminate = -1;
    int stdin_to_close = -1;
    int stdout_to_close = -1;
    
    // Extract state under lock, then release before blocking operations
    {
        std::lock_guard lock(mutex_);

        if (running_ == false) {
            // Also clear starting_ in case stop() is called during start()
            starting_ = false;
            return;
        }

        // Capture values to close/terminate outside the lock
        pid_to_terminate = child_pid_;
        stdin_to_close = stdin_fd_;
        stdout_to_close = stdout_fd_;
        
        // Mark as not running immediately
        running_ = false;
        starting_ = false;
        child_pid_ = -1;
        stdin_fd_ = -1;
        stdout_fd_ = -1;
        
        // Reset read buffer state for potential restart
        read_buffer_pos_ = 0;
        read_buffer_len_ = 0;
    }
    
    // Close pipes outside the lock (non-blocking)
    if (stdin_to_close != -1) {
        close(stdin_to_close);
    }
    if (stdout_to_close != -1) {
        close(stdout_to_close);
    }

    // Terminate child process outside the lock (may block briefly)
    if (pid_to_terminate > 0) {
        kill(pid_to_terminate, SIGTERM);

        // Wait for child to exit (with timeout)
        int status;
        int wait_result = waitpid(pid_to_terminate, &status, WNOHANG);

        if (wait_result == 0) {
            // Child still running, give it a moment (sleep outside lock)
            usleep(kProcessTerminationWaitUs);
            wait_result = waitpid(pid_to_terminate, &status, WNOHANG);

            if (wait_result == 0) {
                // Force kill
                kill(pid_to_terminate, SIGKILL);
                waitpid(pid_to_terminate, &status, 0);
            }
        }
    }
    
    MCPP_LOG_INFO("Stopped process");
}

bool ProcessTransport::is_running() const {
    std::lock_guard lock(mutex_);
    return running_;
}

bool ProcessTransport::is_process_alive() const {
    std::lock_guard lock(mutex_);
    if (child_pid_ <= 0) {
        return false;
    }
    // Check if process is still running without blocking
    int status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);
    return result == 0;  // 0 means still running
}

std::optional<int> ProcessTransport::exit_code() const {
    std::lock_guard lock(mutex_);
    return exit_code_;
}

void ProcessTransport::check_process_status() {
    // Must be called with mutex held
    if (child_pid_ <= 0) {
        return;
    }
    
    int status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);
    
    if (result > 0) {
        // Process has exited
        if (WIFEXITED(status)) {
            exit_code_ = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_code_ = -WTERMSIG(status);  // Negative indicates signal
        }
        running_ = false;
    }
}

bool ProcessTransport::wait_for_readable(int fd, std::chrono::milliseconds timeout) {
    if (timeout.count() == 0) {
        return true;  // No timeout, always proceed
    }

    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;  // Wait for data available to read
    
    int result = poll(&pfd, 1, static_cast<int>(timeout.count()));
    return result > 0 && (pfd.revents & POLLIN);
}

TransportResult<void> ProcessTransport::send(const Json& message) {
    std::lock_guard lock(mutex_);

    if (running_ == false) {
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Process not running"
        ));
    }

    // Check if process is still alive
    check_process_status();
    if (running_ == false) {
        std::string msg = "Process exited";
        if (exit_code_.has_value()) {
            msg += " with code " + std::to_string(*exit_code_);
        }
        return tl::unexpected(make_error(TransportError::Category::Network, msg));
    }

    // Serialize JSON
    std::string body = message.dump();
    std::string data;

    if (config_.use_content_length_framing) {
        // Build framed message (Content-Length header)
        std::ostringstream frame;
        frame << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        data = frame.str();
    } else {
        // Raw JSON per line (newline delimited)
        data = body + "\n";
    }

    // Write to stdin - loop to handle partial writes (POSIX only guarantees
    // atomic writes for small payloads; large JSON frames may be split)
    const char* ptr = data.c_str();
    size_t remaining = data.size();
    
    while (remaining > 0) {
        ssize_t written = write(stdin_fd_, ptr, remaining);
        if (written == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, retry
            }
            return tl::unexpected(make_error(
                TransportError::Category::Network,
                "Failed to write to process: " + std::string(strerror(errno))
            ));
        }
        ptr += written;
        remaining -= static_cast<size_t>(written);
    }

    return {};
}

TransportResult<Json> ProcessTransport::receive() {
    std::lock_guard lock(mutex_);

    if (running_ == false) {
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Process not running"
        ));
    }

    // Check if process is still alive
    check_process_status();
    if (running_ == false) {
        std::string msg = "Process exited";
        if (exit_code_.has_value()) {
            msg += " with code " + std::to_string(*exit_code_);
        }
        return tl::unexpected(make_error(TransportError::Category::Network, msg));
    }

    // Wait for data with timeout (if configured)
    if (config_.read_timeout.count() > 0) {
        if (wait_for_readable(stdout_fd_, config_.read_timeout) == false) {
            return tl::unexpected(make_error(
                TransportError::Category::Timeout,
                "Read timeout"
            ));
        }
    }

    if (config_.use_content_length_framing) {
        return receive_framed();
    } else {
        return receive_line();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffered I/O Helpers
// ─────────────────────────────────────────────────────────────────────────────

TransportResult<std::size_t> ProcessTransport::fill_read_buffer() {
    // Fill buffer from current position
    ssize_t n = ::read(stdout_fd_, read_buffer_, read_buffer_size);
    if (n < 0) {
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Failed to read from process: " + std::string(strerror(errno))
        ));
    }
    if (n == 0) {
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Process closed connection"
        ));
    }
    read_buffer_pos_ = 0;
    read_buffer_len_ = static_cast<std::size_t>(n);
    return read_buffer_len_;
}

TransportResult<void> ProcessTransport::read_exact(char* dest, std::size_t n) {
    std::size_t total_read = 0;
    while (total_read < n) {
        // Use buffered data first
        if (read_buffer_pos_ < read_buffer_len_) {
            std::size_t available = read_buffer_len_ - read_buffer_pos_;
            std::size_t to_copy = std::min(available, n - total_read);
            std::memcpy(dest + total_read, read_buffer_ + read_buffer_pos_, to_copy);
            read_buffer_pos_ += to_copy;
            total_read += to_copy;
        } else {
            // Buffer exhausted, read more
            auto result = fill_read_buffer();
            if (!result) {
                return tl::unexpected(result.error());
            }
        }
    }
    return {};
}

TransportResult<Json> ProcessTransport::receive_line() {
    // Read a line (newline-delimited JSON) using buffered I/O
    std::string line;
    line.reserve(1024);  // Typical JSON-RPC message size
    
    while (true) {
        // Check buffer for newline
        while (read_buffer_pos_ < read_buffer_len_) {
            char c = read_buffer_[read_buffer_pos_++];
            if (c == '\n') {
                // Found newline, parse JSON
                try {
                    return Json::parse(line);
                } catch (const std::exception& e) {
                    return tl::unexpected(make_error(
                        TransportError::Category::Protocol,
                        "Failed to parse JSON: " + std::string(e.what())
                    ));
                }
            }
            line += c;
            
            if (line.size() > config_.max_content_length) {
                return tl::unexpected(make_error(
                    TransportError::Category::Protocol,
                    "Line too large"
                ));
            }
        }
        
        // Buffer exhausted, refill
        auto result = fill_read_buffer();
        if (!result) {
            return tl::unexpected(result.error());
        }
    }
}

TransportResult<Json> ProcessTransport::receive_framed() {
    // Read headers using buffered I/O
    std::string header_buffer;
    header_buffer.reserve(128);  // Typical header size
    
    while (true) {
        // Check buffer for header terminator
        while (read_buffer_pos_ < read_buffer_len_) {
            char c = read_buffer_[read_buffer_pos_++];
            header_buffer += c;
            
            // Check for end of headers (\r\n\r\n)
            const auto sz = header_buffer.size();
            if (sz >= 4 &&
                header_buffer[sz - 4] == '\r' &&
                header_buffer[sz - 3] == '\n' &&
                header_buffer[sz - 2] == '\r' &&
                header_buffer[sz - 1] == '\n') {
                goto headers_complete;
            }
            
            if (sz > 1024) {
                return tl::unexpected(make_error(
                    TransportError::Category::Protocol,
                    "Header too large"
                ));
            }
        }
        
        // Buffer exhausted, refill
        auto result = fill_read_buffer();
        if (!result) {
            return tl::unexpected(result.error());
        }
    }
    
headers_complete:
    // Parse Content-Length (case-insensitive per HTTP spec)
    std::size_t content_length = 0;
    std::string header_lower = header_buffer;
    std::transform(header_lower.begin(), header_lower.end(), header_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    const std::string prefix = "content-length: ";
    auto pos = header_lower.find(prefix);
    if (pos == std::string::npos) {
        return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Missing Content-Length header"
        ));
    }
    auto end_pos = header_buffer.find("\r\n", pos);
    std::string length_str = header_buffer.substr(pos + prefix.size(),
                                                   end_pos - pos - prefix.size());
    try {
        content_length = std::stoull(length_str);
    } catch (...) {
        return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Invalid Content-Length value"
        ));
    }

    if (content_length > config_.max_content_length) {
        return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Content too large"
        ));
    }

    // Read body using buffered read_exact
    std::string body(content_length, '\0');
    auto read_result = read_exact(body.data(), content_length);
    if (!read_result) {
        return tl::unexpected(read_result.error());
    }

    // Parse JSON
    try {
        return Json::parse(body);
    } catch (const std::exception& e) {
        return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Failed to parse JSON: " + std::string(e.what())
        ));
    }
}

}  // namespace mcpp

