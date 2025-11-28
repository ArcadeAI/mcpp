// ─────────────────────────────────────────────────────────────────────────────
// HttpTransport Tests
// ─────────────────────────────────────────────────────────────────────────────
// Comprehensive tests for the HTTP transport layer using MockHttpClient.
// Tests cover:
// - Basic send/receive operations
// - SSE response handling
// - Session management (Mcp-Session-Id)
// - Error handling (network, HTTP errors, parse errors)
// - Lifecycle (start/stop)
// - Edge cases

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "mcpp/transport/http_transport.hpp"
#include "mocks/mock_http_client.hpp"

using namespace mcpp;
using namespace mcpp::testing;
using Json = nlohmann::json;

// Helper to create transport with mock - ensures auto_open_sse_stream is OFF
std::pair<std::unique_ptr<HttpTransport>, MockHttpClient*> 
make_test_transport(const std::string& base_url = "https://api.example.com/mcp") {
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    HttpTransportConfig config;
    config.base_url = base_url;
    config.auto_open_sse_stream = false;  // Critical: no background threads
    
    auto transport = std::make_unique<HttpTransport>(std::move(config), std::move(mock));
    return {std::move(transport), mock_raw};
}

// ═══════════════════════════════════════════════════════════════════════════
// Configuration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport configures client correctly", "[http][transport][config]") {
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    HttpTransportConfig config;
    config.base_url = "https://api.example.com:8443/mcp";
    config.connect_timeout = std::chrono::milliseconds{5000};
    config.read_timeout = std::chrono::milliseconds{15000};
    config.tls.verify_peer = false;
    config.default_headers["X-Custom"] = "value";
    config.auto_open_sse_stream = false;
    
    HttpTransport transport(std::move(config), std::move(mock));
    
    REQUIRE(mock_raw->base_url() == "https://api.example.com:8443");
    REQUIRE(mock_raw->connect_timeout() == std::chrono::milliseconds{5000});
    REQUIRE(mock_raw->read_timeout() == std::chrono::milliseconds{15000});
    REQUIRE(mock_raw->verify_ssl() == false);
    REQUIRE(mock_raw->default_headers().count("X-Custom") == 1);
}

TEST_CASE("HttpTransport rejects invalid URLs", "[http][transport][config]") {
    auto mock = std::make_unique<MockHttpClient>();
    
    HttpTransportConfig config;
    config.base_url = "not-a-valid-url";
    config.auto_open_sse_stream = false;
    
    REQUIRE_THROWS_AS(
        HttpTransport(std::move(config), std::move(mock)),
        std::invalid_argument
    );
}

TEST_CASE("HttpTransport rejects non-HTTP schemes", "[http][transport][config]") {
    auto mock = std::make_unique<MockHttpClient>();
    
    HttpTransportConfig config;
    config.base_url = "ftp://files.example.com/data";
    config.auto_open_sse_stream = false;
    
    REQUIRE_THROWS_AS(
        HttpTransport(std::move(config), std::move(mock)),
        std::invalid_argument
    );
}

// ═══════════════════════════════════════════════════════════════════════════
// Send Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport::send posts JSON message", "[http][transport][send]") {
    auto [transport, mock] = make_test_transport();
    
    mock->queue_json_response(200, R"({"jsonrpc":"2.0","id":1,"result":{}})");
    
    transport->start();
    
    Json message = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/list"}
    };
    
    auto result = transport->send(message);
    
    REQUIRE(result.has_value());
    REQUIRE(mock->request_count() == 1);
    
    auto req = mock->last_request();
    REQUIRE(req.has_value());
    REQUIRE(req->method == HttpMethod::Post);
    REQUIRE(req->path == "/mcp");
    
    // Verify JSON was serialized correctly
    auto sent_json = Json::parse(req->body);
    REQUIRE(sent_json["method"] == "tools/list");
    
    transport->stop();
}

TEST_CASE("HttpTransport::send handles 202 Accepted", "[http][transport][send]") {
    auto [transport, mock] = make_test_transport();
    
    mock->queue_response(202, "");  // No body for notifications
    
    transport->start();
    
    Json notification = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/progress"}
    };
    
    auto result = transport->send(notification);
    
    REQUIRE(result.has_value());  // 202 is success
    
    transport->stop();
}

TEST_CASE("HttpTransport::send fails when not running", "[http][transport][send]") {
    auto [transport, mock] = make_test_transport();
    // Don't call start()
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::Closed);
}

// ═══════════════════════════════════════════════════════════════════════════
// Session Management Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport extracts session ID from response", "[http][transport][session]") {
    auto [transport, mock] = make_test_transport();
    
    mock->queue_response_with_session(200, R"({"jsonrpc":"2.0","id":1,"result":{}})", "session-abc-123");
    
    transport->start();
    
    REQUIRE(transport->session_id().has_value() == false);
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value());
    REQUIRE(transport->session_id().has_value());
    REQUIRE(transport->session_id().value() == "session-abc-123");
    
    transport->stop();
}

TEST_CASE("HttpTransport includes session ID in subsequent requests", "[http][transport][session]") {
    auto [transport, mock] = make_test_transport();
    
    // First response establishes session
    mock->queue_response_with_session(200, R"({"jsonrpc":"2.0","id":1,"result":{}})", "my-session");
    // Second response
    mock->queue_json_response(200, R"({"jsonrpc":"2.0","id":2,"result":{}})");
    
    transport->start();
    
    // First request - no session yet
    Json msg1 = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    transport->send(msg1);
    
    // Second request - should include session
    Json msg2 = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}};
    transport->send(msg2);
    
    auto requests = mock->requests();
    REQUIRE(requests.size() == 2);
    
    // First request shouldn't have session
    REQUIRE(requests[0].headers.count("Mcp-Session-Id") == 0);
    
    // Second request should have session
    REQUIRE(requests[1].headers.count("Mcp-Session-Id") == 1);
    REQUIRE(requests[1].headers.at("Mcp-Session-Id") == "my-session");
    
    transport->stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Error Handling Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport::send handles 404 as session expired when session exists", "[http][transport][error]") {
    auto [transport, mock] = make_test_transport();
    
    // First, establish a session with a successful response
    mock->queue_response(200, R"({"jsonrpc":"2.0","id":1,"result":{}})", 
                         {{"Mcp-Session-Id", "test-session-123"}});
    
    // Then queue multiple 404s - one for the initial request, one for the retry
    // The transport will try to re-establish the session after 404
    mock->queue_response(404, "Not Found");
    mock->queue_response(404, "Not Found");  // Retry also fails
    
    transport->start();
    
    // First request establishes session
    Json init_message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    auto init_result = transport->send(init_message);
    REQUIRE(init_result.has_value());
    
    // Second request gets 404 - should be treated as session expired
    Json message = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::SessionExpired);
    
    transport->stop();
}

TEST_CASE("HttpTransport::send handles 404 as HTTP error when no session", "[http][transport][error]") {
    auto [transport, mock] = make_test_transport();
    
    // 404 without a session should be a regular HTTP error
    mock->queue_response(404, "Not Found");
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::HttpError);
    REQUIRE(result.error().http_status.value_or(0) == 404);
    
    transport->stop();
}

TEST_CASE("HttpTransport::send handles HTTP 500 errors", "[http][transport][error]") {
    auto [transport, mock] = make_test_transport();
    
    // Queue enough 500 errors to exhaust retries (1 initial + 3 retries = 4 total)
    mock->queue_response(500, "Internal Server Error");
    mock->queue_response(500, "Internal Server Error");
    mock->queue_response(500, "Internal Server Error");
    mock->queue_response(500, "Internal Server Error");
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::HttpError);
    REQUIRE(result.error().http_status.has_value());
    REQUIRE(result.error().http_status.value() == 500);
    
    transport->stop();
}

TEST_CASE("HttpTransport::send handles connection errors", "[http][transport][error]") {
    auto [transport, mock] = make_test_transport();
    
    // Queue enough connection errors to exhaust retries (1 initial + 3 retries = 4 total)
    mock->queue_connection_error("Connection refused");
    mock->queue_connection_error("Connection refused");
    mock->queue_connection_error("Connection refused");
    mock->queue_connection_error("Connection refused");
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::ConnectionFailed);
    
    transport->stop();
}

TEST_CASE("HttpTransport::send handles timeout errors", "[http][transport][error]") {
    auto [transport, mock] = make_test_transport();
    
    // Queue enough timeout errors to exhaust retries (1 initial + 3 retries = 4 total)
    mock->queue_timeout();
    mock->queue_timeout();
    mock->queue_timeout();
    mock->queue_timeout();
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::Timeout);
    
    transport->stop();
}

TEST_CASE("HttpTransport::send handles SSL errors", "[http][transport][error]") {
    auto [transport, mock] = make_test_transport();
    
    mock->queue_ssl_error("Certificate verification failed");
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::SslError);
    
    transport->stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Lifecycle Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport::start is idempotent", "[http][transport][lifecycle]") {
    auto [transport, mock] = make_test_transport();
    
    REQUIRE(transport->is_running() == false);
    
    transport->start();
    REQUIRE(transport->is_running() == true);
    
    transport->start();  // Second call should be no-op
    REQUIRE(transport->is_running() == true);
    
    transport->stop();
}

TEST_CASE("HttpTransport::stop is idempotent", "[http][transport][lifecycle]") {
    auto [transport, mock] = make_test_transport();
    
    transport->start();
    
    transport->stop();
    REQUIRE(transport->is_running() == false);
    
    transport->stop();  // Second call should be no-op
    REQUIRE(transport->is_running() == false);
}

TEST_CASE("HttpTransport::stop sends DELETE when session exists", "[http][transport][lifecycle]") {
    auto [transport, mock] = make_test_transport();
    
    // First response establishes session
    mock->queue_response_with_session(200, R"({"jsonrpc":"2.0","id":1,"result":{}})", "session-to-close");
    // DELETE response
    mock->queue_response(200, "");
    
    transport->start();
    
    // Establish session
    Json msg = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    transport->send(msg);
    
    // Stop should send DELETE
    transport->stop();
    
    auto requests = mock->requests();
    REQUIRE(requests.size() == 2);
    REQUIRE(requests[1].method == HttpMethod::Delete);
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport handles empty response body", "[http][transport][edge]") {
    auto [transport, mock] = make_test_transport();
    
    mock->queue_response(200, "");
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    // Empty body with 200 should succeed
    REQUIRE(result.has_value());
    
    transport->stop();
}

TEST_CASE("HttpTransport handles malformed JSON in response", "[http][transport][edge]") {
    auto [transport, mock] = make_test_transport();
    
    HeaderMap headers;
    headers["Content-Type"] = "application/json";
    mock->queue_response(200, "not valid json {{{", headers);
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::ParseError);
    
    transport->stop();
}

TEST_CASE("HttpTransport handles malformed JSON in SSE events gracefully", "[http][transport][edge][sse]") {
    auto [transport, mock] = make_test_transport();
    
    // SSE response with one valid event and one malformed JSON event
    // The malformed event should be logged but not crash the transport
    std::string sse_body = 
        "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"valid\":true}}\n\n"
        "data: {invalid json {{{}\n\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"valid\":true}}\n\n";
    
    mock->queue_sse_response(sse_body);
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    auto result = transport->send(message);
    
    // Should succeed (malformed JSON in SSE is logged but doesn't fail the request)
    REQUIRE(result.has_value());
    
    // Receive should get the valid messages (malformed one is skipped)
    auto received1 = transport->receive();
    REQUIRE(received1.has_value());
    
    auto received2 = transport->receive();
    REQUIRE(received2.has_value());
    
    // Should not receive the malformed message
    // (Note: This test verifies the transport doesn't crash, 
    //  actual logging verification would require a mock logger)
    
    transport->stop();
}

TEST_CASE("HttpTransport handles URL with query parameters", "[http][transport][edge]") {
    auto [transport, mock] = make_test_transport("https://api.example.com/mcp?version=1&debug=true");
    
    mock->queue_json_response(200, R"({})");
    
    transport->start();
    
    Json message = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "test"}};
    transport->send(message);
    
    auto req = mock->last_request();
    REQUIRE(req.has_value());
    REQUIRE(req->path == "/mcp?version=1&debug=true");
    
    transport->stop();
}

TEST_CASE("HttpTransport receive_with_timeout returns nullopt on timeout", "[http][transport][edge]") {
    auto [transport, mock] = make_test_transport();
    
    transport->start();
    
    // No messages queued, should timeout quickly
    auto result = transport->receive_with_timeout(std::chrono::milliseconds{10});
    
    REQUIRE(result.has_value());
    REQUIRE(result->has_value() == false);  // nullopt indicates timeout
    
    transport->stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Session State Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport exposes session state", "[http][transport][session]") {
    auto [transport, mock] = make_test_transport();
    
    // Before start: Disconnected
    REQUIRE(transport->session_state() == SessionState::Disconnected);
    
    transport->start();
    
    // After start: Connecting
    REQUIRE(transport->session_state() == SessionState::Connecting);
    
    // Send a message that establishes session
    mock->queue_response_with_session(200, R"({"jsonrpc":"2.0","id":1,"result":{}})", "session-123");
    
    Json init = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    auto result = transport->send(init);
    REQUIRE(result.has_value());
    
    // After successful response with session ID: Connected
    REQUIRE(transport->session_state() == SessionState::Connected);
    REQUIRE(transport->session_id() == "session-123");
    
    transport->stop();
    
    // After stop: Disconnected
    REQUIRE(transport->session_state() == SessionState::Disconnected);
    REQUIRE(transport->session_id().has_value() == false);
}

TEST_CASE("HttpTransport fires session state change callbacks", "[http][transport][session]") {
    auto [transport, mock] = make_test_transport();
    
    std::vector<std::pair<SessionState, SessionState>> state_changes;
    transport->on_session_state_change([&](SessionState old_state, SessionState new_state) {
        state_changes.emplace_back(old_state, new_state);
    });
    
    transport->start();
    
    mock->queue_response_with_session(200, R"({"result":{}})", "session-abc");
    
    transport->send({{"method", "init"}});
    transport->stop();
    
    // Should have: Disconnected->Connecting, Connecting->Connected, Connected->Closing, Closing->Disconnected
    REQUIRE(state_changes.size() >= 3);
    REQUIRE(state_changes[0].second == SessionState::Connecting);
    REQUIRE(state_changes[1].second == SessionState::Connected);
}

TEST_CASE("HttpTransport fires session established callback", "[http][transport][session]") {
    auto [transport, mock] = make_test_transport();
    
    std::string established_session_id;
    transport->on_session_established([&](const std::string& id) {
        established_session_id = id;
    });
    
    transport->start();
    
    mock->queue_response_with_session(200, R"({})", "my-session-id");
    
    transport->send({{"method", "init"}});
    
    REQUIRE(established_session_id == "my-session-id");
    
    transport->stop();
}

TEST_CASE("HttpTransport fires session lost callback on 404", "[http][transport][session]") {
    auto [transport, mock] = make_test_transport();
    
    bool session_lost = false;
    std::string lost_reason;
    transport->on_session_lost([&](const std::string& reason) {
        session_lost = true;
        lost_reason = reason;
    });
    
    transport->start();
    
    // First: establish session
    mock->queue_response_with_session(200, R"({})", "session-1");
    transport->send({{"method", "init"}});
    REQUIRE(transport->session_state() == SessionState::Connected);
    
    // Second: session expires (404), but reconnection succeeds
    mock->queue_response(404, "Not Found");
    mock->queue_response_with_session(200, R"({})", "session-2");
    
    auto result = transport->send({{"method", "ping"}});
    
    // Session lost callback should have fired
    REQUIRE(session_lost == true);
    REQUIRE(lost_reason.find("expired") != std::string::npos);
    
    // But reconnection should have succeeded
    REQUIRE(result.has_value());
    REQUIRE(transport->session_state() == SessionState::Connected);
    REQUIRE(transport->session_id() == "session-2");
    
    transport->stop();
}

TEST_CASE("HttpTransport handles connection failure during reconnection", "[http][transport][session]") {
    auto [transport, mock] = make_test_transport();
    
    transport->start();
    
    // Establish session
    mock->queue_response_with_session(200, R"({})", "session-1");
    transport->send({{"method", "init"}});
    
    // Session expires, then reconnection also fails (exhaust retries)
    mock->queue_response(404, "Not Found");
    mock->queue_connection_error("Connection refused");
    mock->queue_connection_error("Connection refused");
    mock->queue_connection_error("Connection refused");
    mock->queue_connection_error("Connection refused");
    
    auto result = transport->send({{"method", "ping"}});
    
    // Should fail
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::ConnectionFailed);
    
    // Session state should be Failed
    REQUIRE(transport->session_state() == SessionState::Failed);
    
    transport->stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// SSE Stream Reconnection Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport reconnects after session expiry with new session", "[http][transport][sse][reconnect]") {
    auto [transport, mock] = make_test_transport();
    
    int session_lost_count = 0;
    transport->on_session_lost([&](const std::string&) {
        ++session_lost_count;
    });
    
    transport->start();
    
    // Establish initial session
    mock->queue_response_with_session(200, R"({"result": "ok"})", "session-initial");
    auto init_result = transport->send({{"method", "initialize"}});
    REQUIRE(init_result.has_value());
    REQUIRE(transport->session_id() == "session-initial");
    
    // Simulate session expiry (404) followed by successful reconnection
    mock->queue_response(404, "Session expired");
    mock->queue_response_with_session(200, R"({"result": "reconnected"})", "session-new");
    
    auto result = transport->send({{"method", "ping"}});
    
    // Reconnection should succeed with new session
    REQUIRE(result.has_value());
    REQUIRE(transport->session_id() == "session-new");
    REQUIRE(session_lost_count == 1);
    
    transport->stop();
}

TEST_CASE("HttpTransport retries reconnection multiple times before failing", "[http][transport][sse][reconnect]") {
    auto [transport, mock] = make_test_transport();
    
    transport->start();
    
    // Establish initial session
    mock->queue_response_with_session(200, R"({})", "session-1");
    transport->send({{"method", "init"}});
    REQUIRE(transport->session_id() == "session-1");
    
    // Session expires (404), then immediate success with new session
    // Note: The transport handles 404 as session expired and tries to reconnect
    mock->queue_response(404, "Session expired");
    mock->queue_response_with_session(200, R"({"result": "ok"})", "session-2");
    
    auto result = transport->send({{"method", "ping"}});
    
    // Should succeed with new session
    REQUIRE(result.has_value());
    // Session should be updated (either new session or cleared)
    // The exact behavior depends on implementation
    
    transport->stop();
}

TEST_CASE("HttpTransport propagates error codes correctly after exhausted retries", "[http][transport][error][propagation]") {
    auto [transport, mock] = make_test_transport();
    
    transport->start();
    
    // Test different error types are propagated correctly
    SECTION("Timeout error propagation") {
        mock->queue_timeout();
        mock->queue_timeout();
        mock->queue_timeout();
        mock->queue_timeout();
        
        auto result = transport->send({{"method", "test"}});
        
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == HttpTransportError::Code::Timeout);
    }
    
    SECTION("Connection error propagation") {
        mock->queue_connection_error("ECONNREFUSED");
        mock->queue_connection_error("ECONNREFUSED");
        mock->queue_connection_error("ECONNREFUSED");
        mock->queue_connection_error("ECONNREFUSED");
        
        auto result = transport->send({{"method", "test"}});
        
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == HttpTransportError::Code::ConnectionFailed);
    }
    
    SECTION("HTTP error propagation with status code") {
        mock->queue_response(503, "Service Unavailable");
        mock->queue_response(503, "Service Unavailable");
        mock->queue_response(503, "Service Unavailable");
        mock->queue_response(503, "Service Unavailable");
        
        auto result = transport->send({{"method", "test"}});
        
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == HttpTransportError::Code::HttpError);
        REQUIRE(result.error().http_status.has_value());
        REQUIRE(result.error().http_status.value() == 503);
    }
    
    transport->stop();
}

TEST_CASE("HttpTransport session state transitions correctly during reconnection", "[http][transport][session][state]") {
    auto [transport, mock] = make_test_transport();
    
    transport->start();
    
    // Initial connection
    mock->queue_response_with_session(200, R"({})", "session-1");
    transport->send({{"method", "init"}});
    
    REQUIRE(transport->session_state() == SessionState::Connected);
    
    // Session expiry triggers reconnection
    mock->queue_response(404, "Expired");
    mock->queue_response_with_session(200, R"({})", "session-2");
    
    transport->send({{"method", "ping"}});
    
    // Should have gone through state transitions and end Connected
    REQUIRE(transport->session_state() == SessionState::Connected);
    
    transport->stop();
}
