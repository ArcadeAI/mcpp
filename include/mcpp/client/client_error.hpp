#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// MCP Client Error
// ═══════════════════════════════════════════════════════════════════════════
// Shared error type for both sync and async MCP clients.
//
// This consolidates the error handling logic to avoid duplication between
// McpClient and AsyncMcpClient.

#include "mcpp/protocol/mcp_types.hpp"

#include <tl/expected.hpp>

#include <optional>
#include <string>

namespace mcpp {

/// Error codes for MCP client operations
enum class ClientErrorCode {
    NotConnected,     ///< Client is not connected to server
    NotInitialized,   ///< Client has not completed MCP initialization
    TransportError,   ///< Transport layer error (network, I/O)
    ProtocolError,    ///< Protocol error (invalid response, RPC error)
    Timeout,          ///< Request timed out
    Cancelled         ///< Request was cancelled
};

/// Convert error code to string for logging/debugging
[[nodiscard]] constexpr std::string_view to_string(ClientErrorCode code) noexcept {
    switch (code) {
        case ClientErrorCode::NotConnected:   return "NotConnected";
        case ClientErrorCode::NotInitialized: return "NotInitialized";
        case ClientErrorCode::TransportError: return "TransportError";
        case ClientErrorCode::ProtocolError:  return "ProtocolError";
        case ClientErrorCode::Timeout:        return "Timeout";
        case ClientErrorCode::Cancelled:      return "Cancelled";
        default:                              return "Unknown";  // Unreachable, but silences warnings
    }
}

/// Unified error type for MCP client operations
struct ClientError {
    ClientErrorCode code;
    std::string message;
    std::optional<McpError> rpc_error;  ///< Original RPC error if from server

    // ─────────────────────────────────────────────────────────────────────────
    // Factory Methods
    // ─────────────────────────────────────────────────────────────────────────

    [[nodiscard]] static ClientError not_connected() {
        return {ClientErrorCode::NotConnected, "Client is not connected", std::nullopt};
    }

    [[nodiscard]] static ClientError not_initialized() {
        return {ClientErrorCode::NotInitialized, "Client has not completed initialization", std::nullopt};
    }

    [[nodiscard]] static ClientError transport_error(std::string msg) {
        return {ClientErrorCode::TransportError, std::move(msg), std::nullopt};
    }

    [[nodiscard]] static ClientError protocol_error(std::string msg) {
        return {ClientErrorCode::ProtocolError, std::move(msg), std::nullopt};
    }

    [[nodiscard]] static ClientError timeout_error() {
        return {ClientErrorCode::Timeout, "Request timed out", std::nullopt};
    }
    
    [[nodiscard]] static ClientError timeout(std::string msg) {
        return {ClientErrorCode::Timeout, std::move(msg), std::nullopt};
    }

    [[nodiscard]] static ClientError cancelled() {
        return {ClientErrorCode::Cancelled, "Request was cancelled", std::nullopt};
    }

    [[nodiscard]] static ClientError from_rpc_error(const McpError& err) {
        return {ClientErrorCode::ProtocolError, err.message, err};
    }
};

/// Result type for MCP client operations
template <typename T>
using ClientResult = tl::expected<T, ClientError>;

// ─────────────────────────────────────────────────────────────────────────────
// Legacy Type Aliases (for backwards compatibility)
// ─────────────────────────────────────────────────────────────────────────────
// These preserve the existing API while using the shared implementation.

/// @deprecated Use ClientError instead
using McpClientError = ClientError;

/// @deprecated Use ClientResult instead
template <typename T>
using McpResult = ClientResult<T>;

/// @deprecated Use ClientError instead
using AsyncMcpClientError = ClientError;

/// @deprecated Use ClientResult instead
template <typename T>
using AsyncMcpResult = ClientResult<T>;

}  // namespace mcpp


