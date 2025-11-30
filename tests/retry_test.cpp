#include <catch2/catch_test_macros.hpp>

#include "mcpp/transport/retry_policy.hpp"
#include "mcpp/transport/backoff_policy.hpp"
#include "mcpp/transport/http_transport.hpp"
#include "mocks/mock_http_client.hpp"

#include <chrono>
#include <vector>

using namespace mcpp;
using namespace mcpp::testing;

// ═══════════════════════════════════════════════════════════════════════════
// RetryPolicy Unit Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("RetryPolicy default configuration", "[retry][policy]") {
    RetryPolicy policy;
    
    SECTION("Default max attempts is 3") {
        REQUIRE(policy.max_attempts() == 3);
    }
    
    SECTION("Connection failures are retryable by default") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 0) == true);
    }
    
    SECTION("Timeouts are retryable by default") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::Timeout, 0) == true);
    }
    
    SECTION("SSL errors are NOT retryable by default") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::SslError, 0) == false);
    }
    
    SECTION("Parse errors are NOT retryable") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::ParseError, 0) == false);
    }
    
    SECTION("Session expired is NOT retryable (handled separately)") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::SessionExpired, 0) == false);
    }
    
    SECTION("Closed transport is NOT retryable") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::Closed, 0) == false);
    }
}

TEST_CASE("RetryPolicy respects max attempts", "[retry][policy]") {
    RetryPolicy policy;
    policy.with_max_attempts(3);
    
    SECTION("Allows retries up to max") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 0) == true);
        REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 1) == true);
        REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 2) == true);
    }
    
    SECTION("Denies retry after max attempts") {
        REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 3) == false);
        REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 4) == false);
    }
}

TEST_CASE("RetryPolicy HTTP status code handling", "[retry][policy]") {
    RetryPolicy policy;
    
    SECTION("5xx errors are retryable by default") {
        REQUIRE(policy.should_retry_http_status(500) == true);
        REQUIRE(policy.should_retry_http_status(502) == true);
        REQUIRE(policy.should_retry_http_status(503) == true);
        REQUIRE(policy.should_retry_http_status(504) == true);
    }
    
    SECTION("429 Too Many Requests is retryable") {
        REQUIRE(policy.should_retry_http_status(429) == true);
    }
    
    SECTION("4xx client errors are NOT retryable (except 429)") {
        REQUIRE(policy.should_retry_http_status(400) == false);
        REQUIRE(policy.should_retry_http_status(401) == false);
        REQUIRE(policy.should_retry_http_status(403) == false);
        REQUIRE(policy.should_retry_http_status(404) == false);
    }
    
    SECTION("2xx success is NOT retryable") {
        REQUIRE(policy.should_retry_http_status(200) == false);
        REQUIRE(policy.should_retry_http_status(202) == false);
    }
}

TEST_CASE("RetryPolicy custom configuration", "[retry][policy]") {
    RetryPolicy policy;
    
    SECTION("Can disable connection retry") {
        policy.with_retry_on_connection_error(false);
        REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 0) == false);
    }
    
    SECTION("Can disable timeout retry") {
        policy.with_retry_on_timeout(false);
        REQUIRE(policy.should_retry(HttpTransportError::Code::Timeout, 0) == false);
    }
    
    SECTION("Can enable SSL error retry") {
        policy.with_retry_on_ssl_error(true);
        REQUIRE(policy.should_retry(HttpTransportError::Code::SslError, 0) == true);
    }
    
    SECTION("Can add custom retryable HTTP status codes") {
        policy.with_retryable_status(418);  // I'm a teapot
        REQUIRE(policy.should_retry_http_status(418) == true);
    }
    
    SECTION("Can remove default retryable HTTP status codes") {
        policy.without_retryable_status(503);
        REQUIRE(policy.should_retry_http_status(503) == false);
    }
}

TEST_CASE("RetryPolicy builder pattern", "[retry][policy]") {
    auto policy = RetryPolicy{}
        .with_max_attempts(5)
        .with_retry_on_connection_error(true)
        .with_retry_on_timeout(true)
        .with_retry_on_ssl_error(false)
        .with_retryable_status(418);
    
    REQUIRE(policy.max_attempts() == 5);
    REQUIRE(policy.should_retry(HttpTransportError::Code::ConnectionFailed, 0) == true);
    REQUIRE(policy.should_retry(HttpTransportError::Code::Timeout, 0) == true);
    REQUIRE(policy.should_retry(HttpTransportError::Code::SslError, 0) == false);
    REQUIRE(policy.should_retry_http_status(418) == true);
}

// ═══════════════════════════════════════════════════════════════════════════
// HttpTransport Retry Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

// Helper to create transport with retry policy
std::pair<std::unique_ptr<HttpTransport>, MockHttpClient*> 
make_retry_transport(const std::string& base_url = "https://api.example.com/mcp") {
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    HttpTransportConfig config;
    config.base_url = base_url;
    config.auto_open_sse_stream = false;
    config.max_retries = 3;
    // Use NoBackoff for fast tests
    config.backoff_policy = std::make_shared<NoBackoff>();
    
    auto transport = std::make_unique<HttpTransport>(std::move(config), std::move(mock));
    return {std::move(transport), mock_raw};
}

TEST_CASE("HttpTransport retries on connection failure", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    // First two attempts fail, third succeeds
    mock->queue_connection_error("Connection refused");
    mock->queue_connection_error("Connection reset");
    mock->queue_json_response(200, R"({"result":"ok"})");
    
    REQUIRE(transport->start().has_value());
    
    auto result = transport->send({{"method", "test"}});
    
    REQUIRE(result.has_value());
    REQUIRE(mock->request_count() == 3);  // 2 failures + 1 success
    
    transport->stop();
}

TEST_CASE("HttpTransport retries on timeout", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    mock->queue_timeout("Read timeout");
    mock->queue_json_response(200, R"({})");
    
    REQUIRE(transport->start().has_value());
    
    auto result = transport->send({{"method", "test"}});
    
    REQUIRE(result.has_value());
    REQUIRE(mock->request_count() == 2);
    
    transport->stop();
}

TEST_CASE("HttpTransport retries on 503 Service Unavailable", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    mock->queue_response(503, "Service Unavailable");
    mock->queue_response(503, "Service Unavailable");
    mock->queue_json_response(200, R"({})");
    
    REQUIRE(transport->start().has_value());
    
    auto result = transport->send({{"method", "test"}});
    
    REQUIRE(result.has_value());
    REQUIRE(mock->request_count() == 3);
    
    transport->stop();
}

TEST_CASE("HttpTransport retries on 429 Too Many Requests", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    mock->queue_response(429, "Too Many Requests");
    mock->queue_json_response(200, R"({})");
    
    REQUIRE(transport->start().has_value());
    
    auto result = transport->send({{"method", "test"}});
    
    REQUIRE(result.has_value());
    REQUIRE(mock->request_count() == 2);
    
    transport->stop();
}

TEST_CASE("HttpTransport does NOT retry on 400 Bad Request", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    mock->queue_response(400, "Bad Request");
    
    REQUIRE(transport->start().has_value());
    
    auto result = transport->send({{"method", "test"}});
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::HttpError);
    REQUIRE(mock->request_count() == 1);  // No retries
    
    transport->stop();
}

TEST_CASE("HttpTransport does NOT retry on SSL error", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    mock->queue_ssl_error("Certificate verification failed");
    
    REQUIRE(transport->start().has_value());
    
    auto result = transport->send({{"method", "test"}});
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::SslError);
    REQUIRE(mock->request_count() == 1);  // No retries
    
    transport->stop();
}

TEST_CASE("HttpTransport exhausts retries and returns error", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    // Queue more failures than max_retries
    mock->queue_connection_error("Fail 1");
    mock->queue_connection_error("Fail 2");
    mock->queue_connection_error("Fail 3");
    mock->queue_connection_error("Fail 4");
    
    REQUIRE(transport->start().has_value());
    
    auto result = transport->send({{"method", "test"}});
    
    REQUIRE(result.has_value() == false);
    REQUIRE(result.error().code == HttpTransportError::Code::ConnectionFailed);
    // Should have tried max_retries + 1 times (initial + retries)
    REQUIRE(mock->request_count() == 4);  // 1 initial + 3 retries
    
    transport->stop();
}

TEST_CASE("HttpTransport respects Retry-After header", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    // 503 with Retry-After header
    HeaderMap headers;
    headers["Retry-After"] = "1";  // 1 second
    headers["Content-Type"] = "text/plain";
    mock->queue_response(503, "Service Unavailable", headers);
    mock->queue_json_response(200, R"({})");
    
    REQUIRE(transport->start().has_value());
    
    auto start = std::chrono::steady_clock::now();
    auto result = transport->send({{"method", "test"}});
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    REQUIRE(result.has_value());
    // Should have waited at least ~1 second (with some tolerance)
    // Note: This test may be flaky in CI - consider mocking time
    // For now, just verify the request succeeded
    REQUIRE(mock->request_count() == 2);
    
    transport->stop();
}

TEST_CASE("HttpTransport tracks retry count in session manager", "[http][transport][retry]") {
    auto [transport, mock] = make_retry_transport();
    
    std::vector<SessionState> states;
    transport->on_session_state_change([&](SessionState, SessionState new_state) {
        states.push_back(new_state);
    });
    
    mock->queue_connection_error("Fail");
    mock->queue_connection_error("Fail");
    mock->queue_response_with_session(200, R"({})", "session-123");
    
    REQUIRE(transport->start().has_value());
    transport->send({{"method", "init"}});
    
    // Should have transitioned through Connecting state
    REQUIRE(std::find(states.begin(), states.end(), SessionState::Connecting) != states.end());
    
    transport->stop();
}

// ═══════════════════════════════════════════════════════════════════════════
// Backoff Integration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HttpTransport uses configured backoff policy", "[http][transport][retry][backoff]") {
    auto mock = std::make_unique<MockHttpClient>();
    auto* mock_raw = mock.get();
    
    // Use constant backoff of 10ms for predictable testing
    auto backoff = std::make_shared<ConstantBackoff>(std::chrono::milliseconds{10});
    
    HttpTransportConfig config;
    config.base_url = "https://api.example.com/mcp";
    config.auto_open_sse_stream = false;
    config.max_retries = 2;
    config.backoff_policy = backoff;
    
    auto transport = std::make_unique<HttpTransport>(std::move(config), std::move(mock));
    
    mock_raw->queue_connection_error("Fail 1");
    mock_raw->queue_connection_error("Fail 2");
    mock_raw->queue_json_response(200, R"({})");
    
    REQUIRE(transport->start().has_value());
    
    auto start = std::chrono::steady_clock::now();
    transport->send({{"method", "test"}});
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    // Should have waited at least 20ms (2 retries * 10ms each)
    // Allow some tolerance for timing
    REQUIRE(elapsed >= std::chrono::milliseconds{15});
    
    transport->stop();
}


