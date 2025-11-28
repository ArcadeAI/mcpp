#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Session State
// ─────────────────────────────────────────────────────────────────────────────

/// Represents the current state of the MCP session lifecycle.
///
/// State Machine:
///
///                        ┌──────────────┐
///                        │ Disconnected │◀─────────────────────┐
///                        └──────┬───────┘                      │
///                               │ begin_connect()              │
///                               ▼                              │
///                        ┌──────────────┐                      │
///                   ┌────│  Connecting  │────┐                 │
///                   │    └──────────────┘    │                 │
///      connection_  │                        │ connection_     │
///      established()│                        │ failed()        │
///                   ▼                        ▼                 │
///            ┌──────────────┐         ┌──────────────┐         │
///            │  Connected   │         │    Failed    │─────────┤
///            └──────┬───────┘         └──────────────┘         │
///                   │                        ▲                 │
///    session_       │                        │                 │
///    expired()      │                        │ connection_     │
///                   ▼                        │ failed()        │
///            ┌──────────────┐                │                 │
///            │ Reconnecting │────────────────┘                 │
///            └──────┬───────┘                                  │
///                   │ connection_established()                 │
///                   │                                          │
///                   └──────────────▶ Connected                 │
///                                                              │
///            ┌──────────────┐                                  │
///            │   Closing    │──────────────────────────────────┘
///            └──────────────┘ close_complete()
///
enum class SessionState {
    Disconnected,   ///< No active session, not attempting to connect
    Connecting,     ///< Initial connection attempt in progress
    Connected,      ///< Session established and active
    Reconnecting,   ///< Session expired, attempting to re-establish
    Closing,        ///< Graceful shutdown in progress
    Failed          ///< Connection/reconnection failed
};

/// Convert SessionState to human-readable string.
[[nodiscard]] constexpr std::string_view to_string(SessionState state) noexcept {
    switch (state) {
        case SessionState::Disconnected:  return "Disconnected";
        case SessionState::Connecting:    return "Connecting";
        case SessionState::Connected:     return "Connected";
        case SessionState::Reconnecting:  return "Reconnecting";
        case SessionState::Closing:       return "Closing";
        case SessionState::Failed:        return "Failed";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Session Manager Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct SessionManagerConfig {
    /// Maximum number of automatic reconnection attempts before giving up.
    /// Set to 0 for unlimited attempts.
    std::size_t max_reconnect_attempts{5};

    /// Base delay between reconnection attempts (may be modified by backoff).
    std::chrono::milliseconds reconnect_base_delay{1000};

    /// Maximum delay between reconnection attempts.
    std::chrono::milliseconds reconnect_max_delay{30000};
};

// ─────────────────────────────────────────────────────────────────────────────
// Session Manager
// ─────────────────────────────────────────────────────────────────────────────

/// Manages MCP session lifecycle and state transitions.
///
/// This class is thread-safe for all operations. Callbacks are invoked
/// while holding the internal lock, so callbacks should not call back
/// into the SessionManager (to avoid deadlock).
///
/// Usage:
///   SessionManager manager;
///
///   manager.on_state_change([](SessionState old, SessionState new_state) {
///       std::cout << "State: " << to_string(new_state) << "\n";
///   });
///
///   manager.on_session_established([](const std::string& id) {
///       std::cout << "Session: " << id << "\n";
///   });
///
///   manager.begin_connect();
///   // ... HTTP client establishes connection ...
///   manager.connection_established("session-abc-123");
///
class SessionManager {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Callback Types
    // ─────────────────────────────────────────────────────────────────────────

    /// Called when session state changes.
    using StateChangeCallback = std::function<void(SessionState old_state, SessionState new_state)>;

    /// Called when a new session is established.
    using SessionEstablishedCallback = std::function<void(const std::string& session_id)>;

    /// Called when an active session is lost (expired, network error, etc.).
    using SessionLostCallback = std::function<void(const std::string& reason)>;

    /// Called when reconnection attempts are exhausted.
    using ReconnectExhaustedCallback = std::function<void()>;

    // ─────────────────────────────────────────────────────────────────────────
    // Construction
    // ─────────────────────────────────────────────────────────────────────────

    /// Construct with default configuration.
    SessionManager() = default;

    /// Construct with custom configuration.
    explicit SessionManager(SessionManagerConfig config);

    // Non-copyable, non-movable (due to mutex and callbacks)
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    ~SessionManager() = default;

    // ─────────────────────────────────────────────────────────────────────────
    // State Queries (Thread-Safe)
    // ─────────────────────────────────────────────────────────────────────────

    /// Get current session state.
    [[nodiscard]] SessionState state() const noexcept;

    /// Get current session ID (if connected).
    [[nodiscard]] std::optional<std::string> session_id() const noexcept;

    /// Get last error message (if in Failed state).
    [[nodiscard]] std::string last_error() const noexcept;

    /// Get number of reconnection attempts since last successful connection.
    [[nodiscard]] std::size_t reconnect_count() const noexcept;

    /// Get last SSE event ID (for stream resumption).
    [[nodiscard]] std::optional<std::string> last_event_id() const noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // State Transitions (Thread-Safe)
    // ─────────────────────────────────────────────────────────────────────────

    /// Begin initial connection attempt.
    /// Transition: Disconnected → Connecting
    void begin_connect();

    /// Connection established successfully.
    /// Transition: Connecting|Reconnecting → Connected
    /// Returns false if session_id is invalid (empty, too long, or contains control chars)
    [[nodiscard]] bool connection_established(std::string session_id);
    
    /// Validate session ID format (alphanumeric, hyphens, underscores, max 256 chars)
    [[nodiscard]] static bool is_valid_session_id(std::string_view session_id) noexcept;

    /// Connection attempt failed.
    /// Transition: Connecting|Reconnecting → Failed
    void connection_failed(std::string error_message);

    /// Session expired (server returned 404).
    /// Transition: Connected → Reconnecting
    void session_expired();

    /// Begin graceful shutdown.
    /// Transition: Connected → Closing
    void begin_close();

    /// Graceful shutdown complete.
    /// Transition: Closing → Disconnected
    void close_complete();

    /// Begin reconnection attempt (after failure).
    /// Transition: Failed → Reconnecting
    void begin_reconnect();

    /// Reset to initial state (from any state).
    /// Transition: * → Disconnected
    void reset();

    // ─────────────────────────────────────────────────────────────────────────
    // SSE Event ID Tracking (for stream resumption)
    // ─────────────────────────────────────────────────────────────────────────

    /// Record an SSE event ID (called when receiving events with id field).
    void record_event_id(std::string event_id);

    /// Clear the last event ID (after successful stream resumption).
    void clear_last_event_id();

    // ─────────────────────────────────────────────────────────────────────────
    // Event Callbacks
    // ─────────────────────────────────────────────────────────────────────────

    /// Register callback for state changes.
    void on_state_change(StateChangeCallback callback);

    /// Register callback for session establishment.
    void on_session_established(SessionEstablishedCallback callback);

    /// Register callback for session loss.
    void on_session_lost(SessionLostCallback callback);

    /// Register callback for reconnection exhaustion.
    void on_reconnect_exhausted(ReconnectExhaustedCallback callback);

private:
    // Internal state transition (must hold mutex)
    void transition_to(SessionState new_state);

    // Fire callbacks (called WITHOUT mutex to prevent deadlock)
    void fire_state_change_unlocked(
        SessionState old_state, 
        SessionState new_state,
        const std::vector<StateChangeCallback>& callbacks
    );
    void fire_session_established_unlocked(
        const std::string& id,
        const std::vector<SessionEstablishedCallback>& callbacks
    );
    void fire_session_lost_unlocked(
        const std::string& reason,
        const std::vector<SessionLostCallback>& callbacks
    );
    void fire_reconnect_exhausted_unlocked(
        const std::vector<ReconnectExhaustedCallback>& callbacks
    );

    // Configuration
    SessionManagerConfig config_;

    // State (protected by mutex)
    mutable std::mutex mutex_;
    SessionState state_{SessionState::Disconnected};
    std::optional<std::string> session_id_;
    std::string last_error_;
    std::size_t reconnect_count_{0};
    std::optional<std::string> last_event_id_;

    // Callbacks
    std::vector<StateChangeCallback> state_change_callbacks_;
    std::vector<SessionEstablishedCallback> session_established_callbacks_;
    std::vector<SessionLostCallback> session_lost_callbacks_;
    std::vector<ReconnectExhaustedCallback> reconnect_exhausted_callbacks_;
};

}  // namespace mcpp


