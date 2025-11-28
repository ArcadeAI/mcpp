#ifndef MCPP_TRANSPORT_HTTP_TRANSPORT_CONFIG_HPP
#define MCPP_TRANSPORT_HTTP_TRANSPORT_CONFIG_HPP

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// TLS Configuration
// ─────────────────────────────────────────────────────────────────────────────
// Controls how SSL/TLS connections are established and verified.
// This is critical for security — never disable verification in production!

struct TlsConfig {
    // Path to CA certificate bundle for server verification.
    // If empty, uses system default CA store.
    std::string ca_cert_path;

    // Optional client certificate for mutual TLS (mTLS).
    // Used when server requires client authentication.
    std::optional<std::string> client_cert_path;

    // Private key for client certificate.
    std::optional<std::string> client_key_path;

    // Whether to verify the server's certificate chain.
    // WARNING: Setting to false is a security risk!
    bool verify_peer{true};

    // Whether to verify hostname matches certificate.
    // WARNING: Setting to false is a security risk!
    bool verify_hostname{true};

    // Application-Layer Protocol Negotiation (ALPN) preferences.
    // Common values: "h2" (HTTP/2), "http/1.1"
    // Empty = let OpenSSL choose.
    std::vector<std::string> alpn_protocols;
};

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
struct IBackoffPolicy;
class RetryPolicy;

// ─────────────────────────────────────────────────────────────────────────────
// HTTP Transport Configuration
// ─────────────────────────────────────────────────────────────────────────────
// All settings needed to connect to an MCP server over HTTP.

struct HttpTransportConfig {
    // Type alias for HTTP headers
    using HeaderMap = std::unordered_map<std::string, std::string>;

    // ─────────────────────────────────────────────────────────────────────────
    // Required Settings
    // ─────────────────────────────────────────────────────────────────────────

    // Base URL of the MCP server endpoint.
    // Example: "https://api.example.com/mcp"
    // Must include scheme (http:// or https://).
    std::string base_url;

    // ─────────────────────────────────────────────────────────────────────────
    // Authentication
    // ─────────────────────────────────────────────────────────────────────────

    // Default headers sent with every request.
    // Useful for: Authorization, User-Agent, custom headers.
    HeaderMap default_headers;

    // ─────────────────────────────────────────────────────────────────────────
    // Timeouts
    // ─────────────────────────────────────────────────────────────────────────
    // These prevent hanging indefinitely on slow/dead servers.

    // Maximum time to establish TCP connection.
    std::chrono::milliseconds connect_timeout{10'000};  // 10 seconds

    // Maximum time to wait for data after connection established.
    // For SSE streams, this is the idle timeout between events.
    std::chrono::milliseconds read_timeout{30'000};  // 30 seconds

    // Maximum time to wait for entire request/response cycle.
    // Set to 0 for no limit (useful for long-running SSE streams).
    std::chrono::milliseconds request_timeout{0};  // No limit

    // ─────────────────────────────────────────────────────────────────────────
    // Retry Configuration
    // ─────────────────────────────────────────────────────────────────────────

    // Maximum number of retry attempts for failed requests.
    // 0 = no retries (fail immediately).
    std::size_t max_retries{3};

    // Backoff policy for calculating delay between retries.
    // If null, uses default exponential backoff.
    std::shared_ptr<IBackoffPolicy> backoff_policy;

    // Retry policy defining which errors should trigger retries.
    // If null, uses default retry policy.
    std::shared_ptr<RetryPolicy> retry_policy;

    // ─────────────────────────────────────────────────────────────────────────
    // TLS Settings
    // ─────────────────────────────────────────────────────────────────────────

    // TLS/SSL configuration.
    // Only used when base_url starts with "https://".
    TlsConfig tls;

    // ─────────────────────────────────────────────────────────────────────────
    // MCP-Specific Settings
    // ─────────────────────────────────────────────────────────────────────────

    // Whether to automatically open a GET stream for server-initiated messages.
    // When true, start() opens a persistent SSE connection.
    bool auto_open_sse_stream{true};

    // Delay between SSE reconnection attempts after disconnect/error.
    // Lower values = faster reconnection but more CPU/network usage.
    // Higher values = less aggressive but slower recovery.
    std::chrono::milliseconds sse_reconnect_delay{100};

    // Maximum request body size (prevents OOM from huge JSON payloads).
    // 0 = no limit.
    std::size_t max_request_body_size{10 * 1024 * 1024};  // 10 MiB default

    // ─────────────────────────────────────────────────────────────────────────
    // Builder-Style Helpers
    // ─────────────────────────────────────────────────────────────────────────
    // These return *this for method chaining:
    //   config.with_bearer_token("xxx").with_timeout(5s)

    HttpTransportConfig& with_bearer_token(const std::string& token);
    HttpTransportConfig& with_header(const std::string& name, const std::string& value);
    HttpTransportConfig& with_connect_timeout(std::chrono::milliseconds timeout);
    HttpTransportConfig& with_read_timeout(std::chrono::milliseconds timeout);
    HttpTransportConfig& with_max_retries(std::size_t retries);
    HttpTransportConfig& with_sse_reconnect_delay(std::chrono::milliseconds delay);
};

}  // namespace mcpp

#endif  // MCPP_TRANSPORT_HTTP_TRANSPORT_CONFIG_HPP

