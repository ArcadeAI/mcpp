#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// Async Transport Interface
// ═══════════════════════════════════════════════════════════════════════════
// Modern C++20 coroutine-based transport abstraction using ASIO.
//
// Design principles:
// - Zero-copy where possible (move semantics)
// - Cancellation support via asio::cancellation_slot
// - Composable with any ASIO executor
// - Type-erased executor for flexibility

#include "mcpp/transport.hpp"

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/use_awaitable.hpp>

#include <chrono>
#include <memory>

namespace mcpp::async {

// ═══════════════════════════════════════════════════════════════════════════
// Async Transport Interface
// ═══════════════════════════════════════════════════════════════════════════

class IAsyncTransport {
public:
    virtual ~IAsyncTransport() = default;

    /// Get the executor associated with this transport
    [[nodiscard]] virtual asio::any_io_executor get_executor() = 0;

    /// Start the transport (connect, spawn readers, etc.)
    [[nodiscard]] virtual asio::awaitable<TransportResult<void>> async_start() = 0;

    /// Stop the transport gracefully
    [[nodiscard]] virtual asio::awaitable<void> async_stop() = 0;

    /// Send a JSON message
    [[nodiscard]] virtual asio::awaitable<TransportResult<void>> async_send(Json message) = 0;

    /// Receive a JSON message (blocks until message available or error)
    [[nodiscard]] virtual asio::awaitable<TransportResult<Json>> async_receive() = 0;

    /// Check if transport is running
    [[nodiscard]] virtual bool is_running() const = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Async Transport Configuration
// ═══════════════════════════════════════════════════════════════════════════

struct AsyncTransportConfig {
    /// Maximum time to wait for a response (0 = no timeout)
    std::chrono::milliseconds read_timeout{0};
    
    /// Maximum message size
    std::size_t max_message_size{1 << 20};  // 1 MiB
    
    /// Channel buffer size for incoming messages
    std::size_t receive_buffer_size{16};
};

// ═══════════════════════════════════════════════════════════════════════════
// Factory function (forward declaration)
// ═══════════════════════════════════════════════════════════════════════════

/// Create an async transport that wraps a subprocess
std::unique_ptr<IAsyncTransport> make_async_process_transport(
    asio::any_io_executor executor,
    struct AsyncProcessConfig config
);

}  // namespace mcpp::async


