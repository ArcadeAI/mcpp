#include "mcpp/async/async_process_transport.hpp"
#include "mcpp/log/logger.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <sstream>

namespace mcpp::async {

namespace {

// Named constants for magic numbers
constexpr useconds_t kProcessTerminationWaitUs = 100'000;  // 100ms wait before SIGKILL

TransportError make_error(TransportError::Category cat, const std::string& msg) {
    return TransportError{cat, msg, std::nullopt};
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════════

AsyncProcessTransport::AsyncProcessTransport(
    asio::any_io_executor executor,
    AsyncProcessConfig config
)
    : config_(std::move(config))
    , executor_(std::move(executor))
    , strand_(asio::make_strand(executor_))
{
    read_buffer_.reserve(4096);
}

AsyncProcessTransport::~AsyncProcessTransport() {
    // Synchronous cleanup - can't co_await in destructor
    if (running_) {
        running_ = false;
        terminate_process();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// IAsyncTransport Interface
// ═══════════════════════════════════════════════════════════════════════════

asio::any_io_executor AsyncProcessTransport::get_executor() {
    return executor_;
}

// Platform-specific allowed command prefixes
#if defined(__APPLE__)
const std::vector<std::string> kAllowedCommandPrefixes = {
    "/usr/bin/", "/usr/local/bin/", "/bin/", "/opt/homebrew/bin/",
    "/usr/sbin/", "/sbin/", "/Applications/"
};
#elif defined(__linux__)
const std::vector<std::string> kAllowedCommandPrefixes = {
    "/usr/bin/", "/usr/local/bin/", "/bin/", "/usr/sbin/", "/sbin/",
    "/snap/bin/", "/var/lib/flatpak/", "/home/", "~/.local/bin/"
};
#elif defined(_WIN32)
const std::vector<std::string> kAllowedCommandPrefixes = {
    "C:\\Windows\\System32\\", "C:\\Windows\\", "C:\\Program Files\\",
    "C:\\Program Files (x86)\\", "C:\\Users\\"
};
#else
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

asio::awaitable<TransportResult<void>> AsyncProcessTransport::async_start() {
    if (running_) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Transport already running"
        ));
    }
    
    // Validate command for security (unless explicitly skipped for testing)
    if (!config_.skip_command_validation && !is_safe_command(config_.command, config_.args)) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Command validation failed: potentially unsafe command or arguments"
        ));
    }

    // Spawn the child process (synchronous - creates stream descriptors)
    auto result = spawn_process();
    if (!result) {
        co_return result;
    }

    // Verify streams were created by spawn_process()
    if (!stdin_stream_ || !stdin_stream_->is_open()) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "stdin stream not initialized"
        ));
    }
    if (!stdout_stream_ || !stdout_stream_->is_open()) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "stdout stream not initialized"
        ));
    }

    // Create message channel
    message_channel_ = std::make_unique<MessageChannel>(
        executor_, config_.channel_capacity
    );

    running_ = true;

    // Spawn reader coroutine
    asio::co_spawn(strand_, reader_loop(), asio::detached);

    // Spawn stderr reader if capturing
    if (config_.stderr_handling == StderrHandling::Capture && stderr_stream_ && stderr_stream_->is_open()) {
        asio::co_spawn(strand_, stderr_reader_loop(), asio::detached);
    }

    MCPP_LOG_INFO("AsyncProcessTransport started: " + config_.command);

    co_return TransportResult<void>{};
}

asio::awaitable<void> AsyncProcessTransport::async_stop() {
    if (!running_) {
        co_return;
    }

    running_ = false;

    // Close streams (cancels pending reads/writes)
    if (stdin_stream_ && stdin_stream_->is_open()) {
        asio::error_code ec;
        stdin_stream_->close(ec);
    }
    if (stdout_stream_ && stdout_stream_->is_open()) {
        asio::error_code ec;
        stdout_stream_->close(ec);
    }

    // Close message channel
    if (message_channel_) {
        message_channel_->close();
    }

    // Terminate child process
    terminate_process();

    MCPP_LOG_INFO("AsyncProcessTransport stopped");
}

asio::awaitable<TransportResult<void>> AsyncProcessTransport::async_send(Json message) {
    if (!running_) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Transport not running"
        ));
    }

    // Serialize message
    std::string body = message.dump();
    std::string data;

    if (config_.use_content_length_framing) {
        std::ostringstream frame;
        frame << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        data = frame.str();
    } else {
        data = body + "\n";
    }

    // Async write
    try {
        co_await asio::async_write(
            *stdin_stream_,
            asio::buffer(data),
            asio::use_awaitable
        );
    } catch (const std::system_error& e) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Write failed: " + std::string(e.what())
        ));
    }

    co_return TransportResult<void>{};
}

asio::awaitable<TransportResult<Json>> AsyncProcessTransport::async_receive() {
    if (!running_ && (!message_channel_ || message_channel_->is_open() == false)) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Transport not running"
        ));
    }

    // Receive from channel (populated by reader_loop)
    try {
        auto result = co_await message_channel_->async_receive(asio::use_awaitable);
        co_return result;
    } catch (const std::system_error& e) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Receive failed: " + std::string(e.what())
        ));
    }
}

bool AsyncProcessTransport::is_running() const {
    return running_;
}

// ═══════════════════════════════════════════════════════════════════════════
// Process-Specific Methods
// ═══════════════════════════════════════════════════════════════════════════

pid_t AsyncProcessTransport::child_pid() const {
    return child_pid_;
}

bool AsyncProcessTransport::is_child_alive() const {
    if (child_pid_ <= 0) {
        return false;
    }
    int status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);
    return result == 0;
}

std::optional<int> AsyncProcessTransport::exit_code() const {
    return exit_code_;
}

std::string AsyncProcessTransport::get_stderr() const {
    if (config_.stderr_handling != StderrHandling::Capture) {
        return {};
    }
    std::lock_guard<std::mutex> lock(stderr_mutex_);
    return stderr_buffer_;
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: Reader Loop
// ═══════════════════════════════════════════════════════════════════════════

asio::awaitable<void> AsyncProcessTransport::reader_loop() {
    while (running_) {
        TransportResult<Json> result;

        if (config_.use_content_length_framing) {
            result = co_await read_framed_message();
        } else {
            result = co_await read_line_message();
        }

        if (!result) {
            // Check if it's a graceful shutdown
            if (!running_) {
                break;
            }
            // Push error to channel
            co_await message_channel_->async_send(
                asio::error_code{},
                std::move(result),
                asio::use_awaitable
            );
            break;
        }

        // Push message to channel
        co_await message_channel_->async_send(
            asio::error_code{},
            std::move(result),
            asio::use_awaitable
        );
    }

    // Close channel to signal EOF
    message_channel_->close();
}

asio::awaitable<void> AsyncProcessTransport::stderr_reader_loop() {
    if (!stderr_stream_ || !stderr_stream_->is_open()) {
        co_return;
    }

    std::array<char, 4096> buffer;
    while (running_) {
        try {
            // Use async_read_some for non-blocking reads
            std::size_t n = co_await stderr_stream_->async_read_some(
                asio::buffer(buffer),
                asio::use_awaitable
            );

            if (n == 0) {
                // EOF reached
                break;
            }

            // Append to stderr buffer (thread-safe)
            {
                std::lock_guard<std::mutex> lock(stderr_mutex_);
                stderr_buffer_.append(buffer.data(), n);
            }
        } catch (const std::system_error& e) {
            // Check if it's a graceful shutdown or EOF
            if (!running_ || e.code() == asio::error::eof) {
                break;
            }
            // Log error but continue (stderr capture is best-effort)
            MCPP_LOG_WARN("Stderr read error: " + std::string(e.what()));
            break;
        }
    }
}

asio::awaitable<TransportResult<Json>> AsyncProcessTransport::read_framed_message() {
    // Read headers until \r\n\r\n using persistent read_buffer_
    // async_read_until may read beyond the delimiter, so we must preserve leftover bytes
    try {
        co_await asio::async_read_until(
            *stdout_stream_,
            asio::dynamic_buffer(read_buffer_),
            "\r\n\r\n",
            asio::use_awaitable
        );
        // read_buffer_ may contain more data beyond the delimiter (body data)
    } catch (const std::system_error& e) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Failed to read headers: " + std::string(e.what())
        ));
    }

    // Find the end of headers
    auto header_end = read_buffer_.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Header delimiter not found"
        ));
    }
    
    // Extract headers (excluding the \r\n\r\n delimiter)
    std::string headers = read_buffer_.substr(0, header_end);
    
    // Remove headers + delimiter from buffer, leaving any body data
    read_buffer_.erase(0, header_end + 4);

    // Parse Content-Length (case-insensitive)
    std::string headers_lower = headers;
    std::transform(headers_lower.begin(), headers_lower.end(), headers_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Handle optional whitespace after colon
    const std::string prefix1 = "content-length:";
    auto pos = headers_lower.find(prefix1);
    if (pos == std::string::npos) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Missing Content-Length header"
        ));
    }

    // Skip "content-length:" and any whitespace
    auto value_start = pos + prefix1.size();
    while (value_start < headers.size() && 
           (headers[value_start] == ' ' || headers[value_start] == '\t')) {
        ++value_start;
    }
    
    auto end_pos = headers.find("\r\n", value_start);
    if (end_pos == std::string::npos) {
        end_pos = headers.size();
    }
    std::string length_str = headers.substr(value_start, end_pos - value_start);
    
    std::size_t content_length;
    try {
        content_length = std::stoull(length_str);
    } catch (...) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Invalid Content-Length value: " + length_str
        ));
    }

    if (content_length > config_.max_message_size) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Message too large"
        ));
    }

    // Read remaining body bytes if needed
    // Some body data may already be in read_buffer_ from async_read_until
    if (read_buffer_.size() < content_length) {
        std::size_t bytes_needed = content_length - read_buffer_.size();
        std::size_t old_size = read_buffer_.size();
        read_buffer_.resize(content_length);
        try {
            co_await asio::async_read(
                *stdout_stream_,
                asio::buffer(read_buffer_.data() + old_size, bytes_needed),
                asio::use_awaitable
            );
        } catch (const std::system_error& e) {
            co_return tl::unexpected(make_error(
                TransportError::Category::Network,
                "Failed to read body: " + std::string(e.what())
            ));
        }
    }

    // Extract body from buffer
    std::string body = read_buffer_.substr(0, content_length);
    read_buffer_.erase(0, content_length);

    // Parse JSON
    try {
        co_return Json::parse(body);
    } catch (const std::exception& e) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Failed to parse JSON: " + std::string(e.what())
        ));
    }
}

asio::awaitable<TransportResult<Json>> AsyncProcessTransport::read_line_message() {
    std::string line;
    try {
        std::size_t n = co_await asio::async_read_until(
            *stdout_stream_,
            asio::dynamic_buffer(line),
            '\n',
            asio::use_awaitable
        );
        // Trim to just the line (async_read_until may read more)
        line.resize(n);
        // Remove trailing newline
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
    } catch (const std::system_error& e) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Failed to read line: " + std::string(e.what())
        ));
    }

    if (line.size() > config_.max_message_size) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Line too large"
        ));
    }

    // Parse JSON
    try {
        co_return Json::parse(line);
    } catch (const std::exception& e) {
        co_return tl::unexpected(make_error(
            TransportError::Category::Protocol,
            "Failed to parse JSON: " + std::string(e.what())
        ));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: Process Management
// ═══════════════════════════════════════════════════════════════════════════

TransportResult<void> AsyncProcessTransport::spawn_process() {
    // CRITICAL: Pre-allocate argv BEFORE fork() to avoid malloc deadlock.
    // After fork(), only the calling thread exists in the child. If another
    // thread held the malloc mutex when fork() occurred, any allocation in
    // the child will deadlock forever.
    //
    // We store copies of strings to avoid const_cast (execvp takes char*const[])
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

    // Create pipes
    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Failed to create pipes: " + std::string(strerror(errno))
        ));
    }

    // Create stderr pipe if capturing
    if (config_.stderr_handling == StderrHandling::Capture) {
        if (pipe(stderr_pipe) == -1) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            return tl::unexpected(make_error(
                TransportError::Category::Network,
                "Failed to create stderr pipe: " + std::string(strerror(errno))
            ));
        }
    }

    pid_t pid = fork();

    if (pid == -1) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        if (stderr_pipe[0] != -1) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        return tl::unexpected(make_error(
            TransportError::Category::Network,
            "Failed to fork: " + std::string(strerror(errno))
        ));
    }

    if (pid == 0) {
        // Child process - NO ALLOCATIONS ALLOWED (malloc deadlock risk)
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        // Handle stderr
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
            case StderrHandling::Capture: {
                dup2(stderr_pipe[1], STDERR_FILENO);
                close(stderr_pipe[0]);
                close(stderr_pipe[1]);
                break;
            }
        }

        // Execute (argv was pre-allocated before fork)
        execvp(config_.command.c_str(), argv.data());
        _exit(127);
    }

    // Parent process
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    // Store file descriptors - we'll wrap them in stream_descriptor in async_start
    // For now, create the stream descriptors directly
    stdin_stream_ = std::make_unique<asio::posix::stream_descriptor>(
        executor_, stdin_pipe[1]
    );
    stdout_stream_ = std::make_unique<asio::posix::stream_descriptor>(
        executor_, stdout_pipe[0]
    );

    // Create stderr stream if capturing
    if (config_.stderr_handling == StderrHandling::Capture) {
        close(stderr_pipe[1]);  // Close write end in parent
        stderr_stream_ = std::make_unique<asio::posix::stream_descriptor>(
            executor_, stderr_pipe[0]
        );
    }

    child_pid_ = pid;

    return {};
}

void AsyncProcessTransport::terminate_process() {
    if (child_pid_ <= 0) {
        return;
    }

    // Send SIGTERM
    kill(child_pid_, SIGTERM);

    // Wait briefly
    int status;
    int result = waitpid(child_pid_, &status, WNOHANG);

    if (result == 0) {
        // Still running, wait a bit more
        usleep(kProcessTerminationWaitUs);
        result = waitpid(child_pid_, &status, WNOHANG);

        if (result == 0) {
            // Force kill
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
        }
    }

    // Record exit code
    if (result > 0) {
        if (WIFEXITED(status)) {
            exit_code_ = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_code_ = -WTERMSIG(status);
        }
    }

    child_pid_ = -1;
}

void AsyncProcessTransport::check_child_status() {
    if (child_pid_ <= 0) {
        return;
    }

    int status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);

    if (result > 0) {
        if (WIFEXITED(status)) {
            exit_code_ = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_code_ = -WTERMSIG(status);
        }
        running_ = false;
    }
}

}  // namespace mcpp::async

