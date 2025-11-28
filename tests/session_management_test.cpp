#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "mcpp/transport/session_manager.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace mcpp;
using Catch::Matchers::ContainsSubstring;

// ─────────────────────────────────────────────────────────────────────────────
// SessionState Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionState enum has correct values", "[session][state]") {
    SECTION("All states are distinct") {
        REQUIRE(SessionState::Disconnected != SessionState::Connecting);
        REQUIRE(SessionState::Connecting != SessionState::Connected);
        REQUIRE(SessionState::Connected != SessionState::Reconnecting);
        REQUIRE(SessionState::Reconnecting != SessionState::Closing);
        REQUIRE(SessionState::Closing != SessionState::Failed);
    }

    SECTION("to_string returns readable names") {
        REQUIRE(to_string(SessionState::Disconnected) == "Disconnected");
        REQUIRE(to_string(SessionState::Connecting) == "Connecting");
        REQUIRE(to_string(SessionState::Connected) == "Connected");
        REQUIRE(to_string(SessionState::Reconnecting) == "Reconnecting");
        REQUIRE(to_string(SessionState::Closing) == "Closing");
        REQUIRE(to_string(SessionState::Failed) == "Failed");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SessionManager Basic Lifecycle Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionManager starts in Disconnected state", "[session][lifecycle]") {
    SessionManager manager;
    REQUIRE(manager.state() == SessionState::Disconnected);
    REQUIRE(manager.session_id().has_value() == false);
}

TEST_CASE("SessionManager transitions through states on connect", "[session][lifecycle]") {
    SessionManager manager;
    std::vector<SessionState> observed_states;

    manager.on_state_change([&](SessionState old_state, SessionState new_state) {
        observed_states.push_back(new_state);
    });

    // Simulate successful connection
    manager.begin_connect();
    REQUIRE(manager.state() == SessionState::Connecting);

    manager.connection_established("session-abc-123");
    REQUIRE(manager.state() == SessionState::Connected);
    REQUIRE(manager.session_id() == "session-abc-123");

    // Verify state transitions
    REQUIRE(observed_states.size() == 2);
    REQUIRE(observed_states[0] == SessionState::Connecting);
    REQUIRE(observed_states[1] == SessionState::Connected);
}

TEST_CASE("SessionManager handles connection failure", "[session][lifecycle]") {
    SessionManager manager;
    std::vector<SessionState> observed_states;

    manager.on_state_change([&](SessionState, SessionState new_state) {
        observed_states.push_back(new_state);
    });

    manager.begin_connect();
    manager.connection_failed("Connection refused");

    REQUIRE(manager.state() == SessionState::Failed);
    REQUIRE(manager.last_error() == "Connection refused");

    REQUIRE(observed_states.size() == 2);
    REQUIRE(observed_states[0] == SessionState::Connecting);
    REQUIRE(observed_states[1] == SessionState::Failed);
}

TEST_CASE("SessionManager handles graceful disconnect", "[session][lifecycle]") {
    SessionManager manager;

    // Establish connection first
    manager.begin_connect();
    manager.connection_established("session-123");
    REQUIRE(manager.state() == SessionState::Connected);

    // Graceful close
    manager.begin_close();
    REQUIRE(manager.state() == SessionState::Closing);

    manager.close_complete();
    REQUIRE(manager.state() == SessionState::Disconnected);
    REQUIRE(manager.session_id().has_value() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Session Expiration & Reconnection Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionManager handles session expiration (404)", "[session][expiration]") {
    SessionManager manager;
    std::vector<SessionState> observed_states;

    manager.on_state_change([&](SessionState, SessionState new_state) {
        observed_states.push_back(new_state);
    });

    // Establish connection
    manager.begin_connect();
    manager.connection_established("session-old");
    observed_states.clear();

    // Session expires
    manager.session_expired();
    REQUIRE(manager.state() == SessionState::Reconnecting);
    REQUIRE(manager.session_id().has_value() == false);  // Old session cleared

    // Reconnection succeeds with new session
    manager.connection_established("session-new");
    REQUIRE(manager.state() == SessionState::Connected);
    REQUIRE(manager.session_id() == "session-new");

    REQUIRE(observed_states.size() == 2);
    REQUIRE(observed_states[0] == SessionState::Reconnecting);
    REQUIRE(observed_states[1] == SessionState::Connected);
}

TEST_CASE("SessionManager tracks reconnection attempts", "[session][reconnect]") {
    SessionManager manager;

    manager.begin_connect();
    manager.connection_established("session-1");

    // First expiration
    manager.session_expired();
    REQUIRE(manager.reconnect_count() == 1);

    // Failed reconnect
    manager.connection_failed("Network error");
    REQUIRE(manager.state() == SessionState::Failed);

    // Reset and try again
    manager.reset();
    REQUIRE(manager.state() == SessionState::Disconnected);
    REQUIRE(manager.reconnect_count() == 0);
}

TEST_CASE("SessionManager limits reconnection attempts", "[session][reconnect]") {
    SessionManagerConfig config;
    config.max_reconnect_attempts = 3;

    SessionManager manager(config);
    bool reconnect_exhausted = false;

    manager.on_reconnect_exhausted([&]() {
        reconnect_exhausted = true;
    });

    manager.begin_connect();
    manager.connection_established("session-1");

    // First expiration: reconnect_count becomes 1, state = Reconnecting
    manager.session_expired();
    REQUIRE(manager.reconnect_count() == 1);
    REQUIRE(manager.state() == SessionState::Reconnecting);

    // First reconnect fails - count is 1, not exhausted
    manager.connection_failed("Network error 1");
    REQUIRE(manager.state() == SessionState::Failed);
    REQUIRE(reconnect_exhausted == false);

    // Second attempt: begin_reconnect increments count to 2
    manager.begin_reconnect();
    REQUIRE(manager.reconnect_count() == 2);
    manager.connection_failed("Network error 2");
    REQUIRE(manager.state() == SessionState::Failed);
    REQUIRE(reconnect_exhausted == false);

    // Third attempt: begin_reconnect increments count to 3
    manager.begin_reconnect();
    REQUIRE(manager.reconnect_count() == 3);
    // Now connection_failed should fire exhausted callback
    manager.connection_failed("Network error 3");

    REQUIRE(reconnect_exhausted == true);
    REQUIRE(manager.state() == SessionState::Failed);
}

// ─────────────────────────────────────────────────────────────────────────────
// Event Callbacks Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionManager fires on_session_established callback", "[session][events]") {
    SessionManager manager;
    std::string received_session_id;

    manager.on_session_established([&](const std::string& id) {
        received_session_id = id;
    });

    manager.begin_connect();
    manager.connection_established("my-session-id");

    REQUIRE(received_session_id == "my-session-id");
}

TEST_CASE("SessionManager fires on_session_lost callback", "[session][events]") {
    SessionManager manager;
    bool session_lost_fired = false;
    std::string lost_reason;

    manager.on_session_lost([&](const std::string& reason) {
        session_lost_fired = true;
        lost_reason = reason;
    });

    manager.begin_connect();
    manager.connection_established("session-123");

    manager.session_expired();
    REQUIRE(session_lost_fired == true);
    REQUIRE_THAT(lost_reason, ContainsSubstring("expired"));
}

TEST_CASE("SessionManager supports multiple callbacks", "[session][events]") {
    SessionManager manager;
    int callback_count = 0;

    manager.on_state_change([&](SessionState, SessionState) { callback_count++; });
    manager.on_state_change([&](SessionState, SessionState) { callback_count++; });

    manager.begin_connect();

    // Both callbacks should fire
    REQUIRE(callback_count == 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread Safety Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionManager is thread-safe for state queries", "[session][thread]") {
    SessionManager manager;
    std::atomic<int> read_count{0};
    std::atomic<bool> running{true};

    manager.begin_connect();
    manager.connection_established("session-123");

    // Reader thread
    std::thread reader([&]() {
        while (running.load()) {
            auto state = manager.state();
            auto id = manager.session_id();
            (void)state;
            (void)id;
            read_count++;
        }
    });

    // Writer thread - toggle states
    std::thread writer([&]() {
        for (int i = 0; i < 100; ++i) {
            manager.session_expired();
            manager.connection_established("session-" + std::to_string(i));
        }
    });

    writer.join();
    running = false;
    reader.join();

    // Should have completed without crashes
    REQUIRE(read_count.load() > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Last-Event-ID for SSE Resumption Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionManager tracks last event ID for SSE resumption", "[session][sse]") {
    SessionManager manager;

    manager.begin_connect();
    manager.connection_established("session-123");

    // No last event ID initially
    REQUIRE(manager.last_event_id().has_value() == false);

    // Record event IDs as they arrive
    manager.record_event_id("event-1");
    REQUIRE(manager.last_event_id() == "event-1");

    manager.record_event_id("event-2");
    REQUIRE(manager.last_event_id() == "event-2");

    // After reconnection, last_event_id should be preserved for resumption
    manager.session_expired();
    REQUIRE(manager.last_event_id() == "event-2");  // Still available

    manager.connection_established("session-456");
    // After successful reconnect, can clear if server confirms
    manager.clear_last_event_id();
    REQUIRE(manager.last_event_id().has_value() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// State Machine Invariants Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionManager rejects invalid state transitions", "[session][invariant]") {
    SessionManager manager;

    SECTION("Cannot establish connection without connecting first") {
        // In Disconnected state, connection_established should be rejected
        manager.connection_established("session-123");
        REQUIRE(manager.state() == SessionState::Disconnected);  // No change
    }

    SECTION("Cannot begin_close when not connected") {
        manager.begin_close();
        REQUIRE(manager.state() == SessionState::Disconnected);  // No change
    }

    SECTION("Cannot expire session when not connected") {
        manager.session_expired();
        REQUIRE(manager.state() == SessionState::Disconnected);  // No change
    }
}

TEST_CASE("SessionManager allows reset from any state", "[session][invariant]") {
    SessionManager manager;

    SECTION("Reset from Connecting") {
        manager.begin_connect();
        manager.reset();
        REQUIRE(manager.state() == SessionState::Disconnected);
    }

    SECTION("Reset from Connected") {
        manager.begin_connect();
        manager.connection_established("session-123");
        manager.reset();
        REQUIRE(manager.state() == SessionState::Disconnected);
        REQUIRE(manager.session_id().has_value() == false);
    }

    SECTION("Reset from Failed") {
        manager.begin_connect();
        manager.connection_failed("error");
        manager.reset();
        REQUIRE(manager.state() == SessionState::Disconnected);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Session ID Validation Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SessionManager validates session ID format", "[session][security]") {
    SECTION("Valid session IDs are accepted") {
        REQUIRE(SessionManager::is_valid_session_id("session-123"));
        REQUIRE(SessionManager::is_valid_session_id("abc_def.ghi"));
        REQUIRE(SessionManager::is_valid_session_id("ABC123"));
        REQUIRE(SessionManager::is_valid_session_id("a"));
        REQUIRE(SessionManager::is_valid_session_id("session-abc-123-def-456"));
    }
    
    SECTION("Empty session ID is rejected") {
        REQUIRE_FALSE(SessionManager::is_valid_session_id(""));
    }
    
    SECTION("Session ID with control characters is rejected") {
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session\n123"));
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session\r123"));
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session\t123"));
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session\0123"));
    }
    
    SECTION("Session ID with special characters is rejected") {
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session;123"));
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session<script>"));
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session 123"));
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session/123"));
        REQUIRE_FALSE(SessionManager::is_valid_session_id("session:123"));
    }
    
    SECTION("Overly long session ID is rejected") {
        std::string long_id(257, 'a');  // 257 chars > max 256
        REQUIRE_FALSE(SessionManager::is_valid_session_id(long_id));
        
        std::string max_id(256, 'a');  // Exactly 256 chars
        REQUIRE(SessionManager::is_valid_session_id(max_id));
    }
}

TEST_CASE("SessionManager rejects invalid session IDs in connection_established", "[session][security]") {
    SessionManager manager;
    manager.begin_connect();
    
    SECTION("Invalid session ID returns false and doesn't change state") {
        REQUIRE_FALSE(manager.connection_established("session\n123"));
        REQUIRE(manager.state() == SessionState::Connecting);
        REQUIRE_FALSE(manager.session_id().has_value());
    }
    
    SECTION("Valid session ID returns true and changes state") {
        REQUIRE(manager.connection_established("valid-session-123"));
        REQUIRE(manager.state() == SessionState::Connected);
        REQUIRE(manager.session_id().has_value());
        REQUIRE(manager.session_id().value() == "valid-session-123");
    }
}


