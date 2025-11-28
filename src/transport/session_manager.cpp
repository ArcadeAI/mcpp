#include "mcpp/transport/session_manager.hpp"

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

SessionManager::SessionManager(SessionManagerConfig config)
    : config_(std::move(config))
{}

// ─────────────────────────────────────────────────────────────────────────────
// State Queries
// ─────────────────────────────────────────────────────────────────────────────

SessionState SessionManager::state() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::optional<std::string> SessionManager::session_id() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_id_;
}

std::string SessionManager::last_error() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

std::size_t SessionManager::reconnect_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return reconnect_count_;
}

std::optional<std::string> SessionManager::last_event_id() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_event_id_;
}

// ─────────────────────────────────────────────────────────────────────────────
// State Transitions
// ─────────────────────────────────────────────────────────────────────────────

void SessionManager::begin_connect() {
    std::vector<StateChangeCallback> callbacks;
    SessionState old_state, new_state;
    bool should_fire = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only valid from Disconnected state
        const bool is_disconnected = (state_ == SessionState::Disconnected);
        if (is_disconnected == false) {
            return;
        }

        old_state = state_;
        new_state = SessionState::Connecting;
        state_ = new_state;
        callbacks = state_change_callbacks_;
        should_fire = true;
    }
    
    if (should_fire) {
        fire_state_change_unlocked(old_state, new_state, callbacks);
    }
}

bool SessionManager::is_valid_session_id(std::string_view session_id) noexcept {
    // Empty session IDs are invalid
    if (session_id.empty()) {
        return false;
    }
    
    // Max length to prevent memory issues (256 is generous for session IDs)
    constexpr std::size_t max_session_id_length = 256;
    if (session_id.size() > max_session_id_length) {
        return false;
    }
    
    // Only allow safe characters: alphanumeric, hyphen, underscore, dot
    // This prevents log injection and other attacks
    for (char c : session_id) {
        const bool is_alphanumeric = (c >= 'a' && c <= 'z') || 
                                      (c >= 'A' && c <= 'Z') || 
                                      (c >= '0' && c <= '9');
        const bool is_safe_special = (c == '-') || (c == '_') || (c == '.');
        if (!is_alphanumeric && !is_safe_special) {
            return false;
        }
    }
    
    return true;
}

bool SessionManager::connection_established(std::string session_id) {
    // Validate session ID before accepting
    if (!is_valid_session_id(session_id)) {
        return false;
    }
    
    std::vector<StateChangeCallback> state_callbacks;
    std::vector<SessionEstablishedCallback> established_callbacks;
    SessionState old_state, new_state;
    std::string established_id;
    bool should_fire = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only valid from Connecting or Reconnecting states
        const bool is_connecting = (state_ == SessionState::Connecting);
        const bool is_reconnecting = (state_ == SessionState::Reconnecting);
        const bool can_establish = is_connecting || is_reconnecting;

        if (can_establish == false) {
            return false;
        }

        session_id_ = std::move(session_id);
        last_error_.clear();

        old_state = state_;
        new_state = SessionState::Connected;
        state_ = new_state;
        established_id = *session_id_;
        state_callbacks = state_change_callbacks_;
        established_callbacks = session_established_callbacks_;
        should_fire = true;
    }
    
    if (should_fire) {
        fire_state_change_unlocked(old_state, new_state, state_callbacks);
        fire_session_established_unlocked(established_id, established_callbacks);
    }
    return true;
}

void SessionManager::connection_failed(std::string error_message) {
    std::vector<StateChangeCallback> state_callbacks;
    std::vector<ReconnectExhaustedCallback> exhausted_callbacks;
    SessionState old_state, new_state;
    bool should_fire_state = false;
    bool should_fire_exhausted = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only valid from Connecting or Reconnecting states
        const bool is_connecting = (state_ == SessionState::Connecting);
        const bool is_reconnecting = (state_ == SessionState::Reconnecting);
        const bool can_fail = is_connecting || is_reconnecting;

        if (can_fail == false) {
            return;
        }

        last_error_ = std::move(error_message);

        // Check if we've exhausted reconnection attempts
        const bool has_limit = (config_.max_reconnect_attempts > 0);
        const bool exhausted = has_limit && (reconnect_count_ >= config_.max_reconnect_attempts);

        if (exhausted) {
            exhausted_callbacks = reconnect_exhausted_callbacks_;
            should_fire_exhausted = true;
        }

        old_state = state_;
        new_state = SessionState::Failed;
        state_ = new_state;
        state_callbacks = state_change_callbacks_;
        should_fire_state = true;
    }
    
    if (should_fire_exhausted) {
        fire_reconnect_exhausted_unlocked(exhausted_callbacks);
    }
    if (should_fire_state) {
        fire_state_change_unlocked(old_state, new_state, state_callbacks);
    }
}

void SessionManager::session_expired() {
    std::vector<StateChangeCallback> state_callbacks;
    std::vector<SessionLostCallback> lost_callbacks;
    SessionState old_state, new_state;
    bool should_fire = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only valid from Connected state
        const bool is_connected = (state_ == SessionState::Connected);
        if (is_connected == false) {
            return;
        }

        // Clear session ID but preserve last_event_id for resumption
        session_id_.reset();
        reconnect_count_++;

        old_state = state_;
        new_state = SessionState::Reconnecting;
        state_ = new_state;
        state_callbacks = state_change_callbacks_;
        lost_callbacks = session_lost_callbacks_;
        should_fire = true;
    }
    
    if (should_fire) {
        fire_session_lost_unlocked("Session expired (404)", lost_callbacks);
        fire_state_change_unlocked(old_state, new_state, state_callbacks);
    }
}

void SessionManager::begin_close() {
    std::vector<StateChangeCallback> callbacks;
    SessionState old_state, new_state;
    bool should_fire = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only valid from Connected state
        const bool is_connected = (state_ == SessionState::Connected);
        if (is_connected == false) {
            return;
        }

        old_state = state_;
        new_state = SessionState::Closing;
        state_ = new_state;
        callbacks = state_change_callbacks_;
        should_fire = true;
    }
    
    if (should_fire) {
        fire_state_change_unlocked(old_state, new_state, callbacks);
    }
}

void SessionManager::close_complete() {
    std::vector<StateChangeCallback> callbacks;
    SessionState old_state, new_state;
    bool should_fire = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only valid from Closing state
        const bool is_closing = (state_ == SessionState::Closing);
        if (is_closing == false) {
            return;
        }

        session_id_.reset();
        last_event_id_.reset();
        reconnect_count_ = 0;

        old_state = state_;
        new_state = SessionState::Disconnected;
        state_ = new_state;
        callbacks = state_change_callbacks_;
        should_fire = true;
    }
    
    if (should_fire) {
        fire_state_change_unlocked(old_state, new_state, callbacks);
    }
}

void SessionManager::begin_reconnect() {
    std::vector<StateChangeCallback> callbacks;
    SessionState old_state, new_state;
    bool should_fire = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Only valid from Failed state
        const bool is_failed = (state_ == SessionState::Failed);
        if (is_failed == false) {
            return;
        }

        // Increment reconnect count for this retry attempt
        reconnect_count_++;

        old_state = state_;
        new_state = SessionState::Reconnecting;
        state_ = new_state;
        callbacks = state_change_callbacks_;
        should_fire = true;
    }
    
    if (should_fire) {
        fire_state_change_unlocked(old_state, new_state, callbacks);
    }
}

void SessionManager::reset() {
    std::vector<StateChangeCallback> callbacks;
    SessionState old_state;
    bool should_fire = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);

        session_id_.reset();
        last_event_id_.reset();
        last_error_.clear();
        reconnect_count_ = 0;

        old_state = state_;
        state_ = SessionState::Disconnected;

        const bool state_changed = (old_state != SessionState::Disconnected);
        if (state_changed) {
            callbacks = state_change_callbacks_;
            should_fire = true;
        }
    }
    
    if (should_fire) {
        fire_state_change_unlocked(old_state, SessionState::Disconnected, callbacks);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SSE Event ID Tracking
// ─────────────────────────────────────────────────────────────────────────────

void SessionManager::record_event_id(std::string event_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_event_id_ = std::move(event_id);
}

void SessionManager::clear_last_event_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_event_id_.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Event Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void SessionManager::on_state_change(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_change_callbacks_.push_back(std::move(callback));
}

void SessionManager::on_session_established(SessionEstablishedCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_established_callbacks_.push_back(std::move(callback));
}

void SessionManager::on_session_lost(SessionLostCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_lost_callbacks_.push_back(std::move(callback));
}

void SessionManager::on_reconnect_exhausted(ReconnectExhaustedCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    reconnect_exhausted_callbacks_.push_back(std::move(callback));
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

void SessionManager::transition_to(SessionState new_state) {
    // Caller must hold mutex
    // Note: This method now only updates state. Callbacks are fired by callers
    // after releasing the mutex to prevent deadlocks.
    state_ = new_state;
}

void SessionManager::fire_state_change_unlocked(
    SessionState old_state, 
    SessionState new_state,
    const std::vector<StateChangeCallback>& callbacks
) {
    // Called WITHOUT mutex held to prevent deadlock
    for (const auto& callback : callbacks) {
        callback(old_state, new_state);
    }
}

void SessionManager::fire_session_established_unlocked(
    const std::string& id,
    const std::vector<SessionEstablishedCallback>& callbacks
) {
    // Called WITHOUT mutex held to prevent deadlock
    for (const auto& callback : callbacks) {
        callback(id);
    }
}

void SessionManager::fire_session_lost_unlocked(
    const std::string& reason,
    const std::vector<SessionLostCallback>& callbacks
) {
    // Called WITHOUT mutex held to prevent deadlock
    for (const auto& callback : callbacks) {
        callback(reason);
    }
}

void SessionManager::fire_reconnect_exhausted_unlocked(
    const std::vector<ReconnectExhaustedCallback>& callbacks
) {
    // Called WITHOUT mutex held to prevent deadlock
    for (const auto& callback : callbacks) {
        callback();
    }
}

}  // namespace mcpp

