// ─────────────────────────────────────────────────────────────────────────────
// HTTP Transport Config & URL Parsing Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for:
// - HttpTransportConfig builder methods
// - TlsConfig defaults
// - URL parsing (various formats)
// - Backoff policies

#include <catch2/catch_test_macros.hpp>

#include "mcpp/transport/http_transport_config.hpp"
#include "mcpp/transport/http_types.hpp"
#include "mcpp/transport/backoff_policy.hpp"

using namespace mcpp;
using namespace std::chrono_literals;

// ═══════════════════════════════════════════════════════════════════════════
// HttpTransportConfig Tests
// ═══════════════════════════════════════════════════════════════════════════

SCENARIO("HttpTransportConfig has sensible defaults", "[http][config]") {
    GIVEN("a default-constructed config") {
        HttpTransportConfig config;

        THEN("timeouts have reasonable defaults") {
            REQUIRE(config.connect_timeout == 10'000ms);
            REQUIRE(config.read_timeout == 30'000ms);
            REQUIRE(config.request_timeout == 0ms);  // No limit
        }

        THEN("retry settings have defaults") {
            REQUIRE(config.max_retries == 3);
            REQUIRE(config.backoff_policy == nullptr);  // Use default
        }

        THEN("TLS verification is enabled by default") {
            REQUIRE(config.tls.verify_peer == true);
            REQUIRE(config.tls.verify_hostname == true);
        }

        THEN("auto SSE stream is enabled") {
            REQUIRE(config.auto_open_sse_stream == true);
        }
    }
}

SCENARIO("HttpTransportConfig builder methods work", "[http][config]") {
    GIVEN("a config with base URL") {
        HttpTransportConfig config;
        config.base_url = "https://api.example.com/mcp";

        WHEN("adding a bearer token") {
            config.with_bearer_token("secret-token-123");

            THEN("Authorization header is set correctly") {
                const auto it = config.default_headers.find("Authorization");
                REQUIRE(it != config.default_headers.end());
                REQUIRE(it->second == "Bearer secret-token-123");
            }
        }

        WHEN("chaining multiple builders") {
            config.with_bearer_token("token")
                  .with_header("X-Custom", "value")
                  .with_connect_timeout(5s)
                  .with_read_timeout(15s)
                  .with_max_retries(5);

            THEN("all settings are applied") {
                REQUIRE(config.default_headers["Authorization"] == "Bearer token");
                REQUIRE(config.default_headers["X-Custom"] == "value");
                REQUIRE(config.connect_timeout == 5s);
                REQUIRE(config.read_timeout == 15s);
                REQUIRE(config.max_retries == 5);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// URL Parsing Tests
// ═══════════════════════════════════════════════════════════════════════════

SCENARIO("parse_url handles standard HTTPS URLs", "[http][url]") {
    GIVEN("a simple HTTPS URL") {
        const std::string url = "https://api.example.com/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("all components are extracted") {
                REQUIRE(result.has_value());
                REQUIRE(result->scheme == "https");
                REQUIRE(result->host == "api.example.com");
                REQUIRE(result->port == 443);
                REQUIRE(result->path == "/mcp");
                REQUIRE(result->query.empty());
                REQUIRE(result->is_secure() == true);
            }
        }
    }

    GIVEN("an HTTPS URL with explicit port") {
        const std::string url = "https://api.example.com:8443/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("explicit port is used") {
                REQUIRE(result.has_value());
                REQUIRE(result->port == 8443);
            }
        }
    }

    GIVEN("an HTTPS URL with query string") {
        const std::string url = "https://api.example.com/mcp?debug=true&verbose=1";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("query is preserved") {
                REQUIRE(result.has_value());
                REQUIRE(result->path == "/mcp");
                REQUIRE(result->query == "?debug=true&verbose=1");
                REQUIRE(result->path_with_query() == "/mcp?debug=true&verbose=1");
            }
        }
    }
}

SCENARIO("parse_url handles HTTP URLs", "[http][url]") {
    GIVEN("a simple HTTP URL") {
        const std::string url = "http://localhost:8080/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("HTTP scheme and port are correct") {
                REQUIRE(result.has_value());
                REQUIRE(result->scheme == "http");
                REQUIRE(result->host == "localhost");
                REQUIRE(result->port == 8080);
                REQUIRE(result->is_secure() == false);
            }
        }
    }

    GIVEN("an HTTP URL without port") {
        const std::string url = "http://example.com/path";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("default port 80 is used") {
                REQUIRE(result.has_value());
                REQUIRE(result->port == 80);
            }
        }
    }
}

SCENARIO("parse_url handles edge cases", "[http][url]") {
    GIVEN("a URL with no path") {
        const std::string url = "https://api.example.com";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("path defaults to /") {
                REQUIRE(result.has_value());
                REQUIRE(result->path == "/");
            }
        }
    }

    GIVEN("a URL with userinfo") {
        const std::string url = "https://user:pass@api.example.com/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("userinfo is stripped, host is correct") {
                REQUIRE(result.has_value());
                REQUIRE(result->host == "api.example.com");
            }
        }
    }

    GIVEN("a URL with mixed-case scheme") {
        const std::string url = "HTTPS://api.example.com/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("scheme is normalized to lowercase") {
                REQUIRE(result.has_value());
                REQUIRE(result->scheme == "https");
            }
        }
    }
}

SCENARIO("parse_url rejects invalid URLs", "[http][url]") {
    GIVEN("a URL with missing scheme") {
        const std::string url = "api.example.com/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("parsing fails") {
                REQUIRE(result.has_value() == false);
            }
        }
    }

    GIVEN("a URL with unsupported scheme") {
        const std::string url = "ftp://files.example.com/data";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("parsing fails") {
                REQUIRE(result.has_value() == false);
            }
        }
    }

    GIVEN("a URL with invalid port") {
        const std::string url = "https://api.example.com:notaport/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("parsing fails") {
                REQUIRE(result.has_value() == false);
            }
        }
    }

    GIVEN("an empty URL") {
        const std::string url = "";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("parsing fails") {
                REQUIRE(result.has_value() == false);
            }
        }
    }

    // Note: WHATWG URL spec allows "https:///path" (empty host for file-like paths)
    // but our parse_url rejects empty hosts, so we test with a clearly invalid URL
    GIVEN("a URL with invalid host characters") {
        const std::string url = "https://[invalid/mcp";

        WHEN("parsing") {
            const auto result = parse_url(url);

            THEN("parsing fails") {
                REQUIRE(result.has_value() == false);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Case-Insensitive Header Lookup Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("find_header performs case-insensitive lookup", "[http][headers]") {
    HeaderMap headers;
    headers["Content-Type"] = "application/json";
    headers["X-Custom-Header"] = "value";
    
    SECTION("Exact case match works") {
        auto it = find_header(headers, "Content-Type");
        REQUIRE(it != headers.end());
        REQUIRE(it->second == "application/json");
    }
    
    SECTION("Lowercase lookup works") {
        auto it = find_header(headers, "content-type");
        REQUIRE(it != headers.end());
        REQUIRE(it->second == "application/json");
    }
    
    SECTION("Uppercase lookup works") {
        auto it = find_header(headers, "CONTENT-TYPE");
        REQUIRE(it != headers.end());
        REQUIRE(it->second == "application/json");
    }
    
    SECTION("Mixed case lookup works") {
        auto it = find_header(headers, "CoNtEnT-TyPe");
        REQUIRE(it != headers.end());
        REQUIRE(it->second == "application/json");
    }
    
    SECTION("Non-existent header returns end()") {
        auto it = find_header(headers, "Authorization");
        REQUIRE(it == headers.end());
    }
}

TEST_CASE("get_header returns optional value", "[http][headers]") {
    HeaderMap headers;
    headers["Retry-After"] = "120";
    headers["retry-after"] = "60";  // Different casing (map stores both!)
    
    SECTION("Returns value for existing header") {
        auto value = get_header(headers, "retry-after");
        REQUIRE(value.has_value());
        // Will find one of them (order unspecified in unordered_map)
        const bool valid_value = (*value == "120" || *value == "60");
        REQUIRE(valid_value);
    }
    
    SECTION("Returns nullopt for missing header") {
        auto value = get_header(headers, "X-Missing");
        REQUIRE(value.has_value() == false);
    }
}

TEST_CASE("HTTP headers from different servers are handled correctly", "[http][headers]") {
    SECTION("nginx style (lowercase)") {
        HeaderMap headers;
        headers["content-type"] = "text/event-stream";
        
        auto value = get_header(headers, "Content-Type");
        REQUIRE(value.has_value());
        REQUIRE(*value == "text/event-stream");
    }
    
    SECTION("Apache style (mixed case)") {
        HeaderMap headers;
        headers["Content-Type"] = "application/json";
        
        auto value = get_header(headers, "content-type");
        REQUIRE(value.has_value());
        REQUIRE(*value == "application/json");
    }
    
    SECTION("All caps (unusual but valid)") {
        HeaderMap headers;
        headers["RETRY-AFTER"] = "30";
        
        auto value = get_header(headers, "Retry-After");
        REQUIRE(value.has_value());
        REQUIRE(*value == "30");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Backoff Policy Tests
// ═══════════════════════════════════════════════════════════════════════════

SCENARIO("ExponentialBackoff calculates delays correctly", "[http][backoff]") {
    GIVEN("an exponential backoff with no jitter") {
        // base=100ms, multiplier=2, max=1s, jitter=0
        ExponentialBackoff backoff(100ms, 2.0, 1000ms, 0.0);

        THEN("delays grow exponentially") {
            REQUIRE(backoff.next_delay(0) == 100ms);   // 100 * 2^0
            REQUIRE(backoff.next_delay(1) == 200ms);   // 100 * 2^1
            REQUIRE(backoff.next_delay(2) == 400ms);   // 100 * 2^2
            REQUIRE(backoff.next_delay(3) == 800ms);   // 100 * 2^3
        }

        THEN("delays are capped at max") {
            REQUIRE(backoff.next_delay(4) == 1000ms);  // Would be 1600, capped
            REQUIRE(backoff.next_delay(10) == 1000ms); // Way over, still capped
        }
    }
}

SCENARIO("ExponentialBackoff adds jitter", "[http][backoff]") {
    GIVEN("an exponential backoff with 25% jitter") {
        ExponentialBackoff backoff(100ms, 2.0, 10000ms, 0.25);

        WHEN("getting delay for attempt 0 multiple times") {
            // With 25% jitter, 100ms becomes 75-125ms
            bool saw_different_values = false;
            auto first_delay = backoff.next_delay(0);

            for (int i = 0; i < 10; ++i) {
                auto delay = backoff.next_delay(0);
                if (delay != first_delay) {
                    saw_different_values = true;
                    break;
                }
            }

            THEN("values vary due to jitter") {
                // Note: This test is probabilistic. With 25% jitter,
                // it's extremely unlikely to get the same value 10 times.
                REQUIRE(saw_different_values == true);
            }
        }

        THEN("delays are within expected jitter range") {
            // For attempt 0: base 100ms, ±25% = 75-125ms
            for (int i = 0; i < 20; ++i) {
                auto delay = backoff.next_delay(0);
                REQUIRE(delay >= 75ms);
                REQUIRE(delay <= 125ms);
            }
        }
    }
}

SCENARIO("NoBackoff returns zero delay", "[http][backoff]") {
    GIVEN("a no-backoff policy") {
        NoBackoff backoff;

        THEN("all delays are zero") {
            REQUIRE(backoff.next_delay(0) == 0ms);
            REQUIRE(backoff.next_delay(1) == 0ms);
            REQUIRE(backoff.next_delay(100) == 0ms);
        }
    }
}

SCENARIO("ConstantBackoff returns fixed delay", "[http][backoff]") {
    GIVEN("a constant backoff of 500ms") {
        ConstantBackoff backoff(500ms);

        THEN("all delays are 500ms") {
            REQUIRE(backoff.next_delay(0) == 500ms);
            REQUIRE(backoff.next_delay(1) == 500ms);
            REQUIRE(backoff.next_delay(100) == 500ms);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// HttpResponse Helper Tests
// ═══════════════════════════════════════════════════════════════════════════

SCENARIO("HttpResponse status helpers work correctly", "[http][response]") {
    GIVEN("various HTTP responses") {
        HttpResponse ok;
        ok.status_code = 200;

        HttpResponse accepted;
        accepted.status_code = 202;

        HttpResponse redirect;
        redirect.status_code = 301;

        HttpResponse not_found;
        not_found.status_code = 404;

        HttpResponse server_error;
        server_error.status_code = 500;

        THEN("status helpers return correct values") {
            REQUIRE(ok.is_success() == true);
            REQUIRE(accepted.is_success() == true);
            REQUIRE(redirect.is_success() == false);
            REQUIRE(redirect.is_redirect() == true);
            REQUIRE(not_found.is_client_error() == true);
            REQUIRE(server_error.is_server_error() == true);
        }
    }
}

SCENARIO("HttpResponse content type detection works", "[http][response]") {
    GIVEN("an SSE response") {
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "text/event-stream";

        THEN("is_sse returns true") {
            REQUIRE(response.is_sse() == true);
            REQUIRE(response.is_json() == false);
        }
    }

    GIVEN("a JSON response") {
        HttpResponse response;
        response.status_code = 200;
        response.headers["Content-Type"] = "application/json; charset=utf-8";

        THEN("is_json returns true") {
            REQUIRE(response.is_json() == true);
            REQUIRE(response.is_sse() == false);
        }
    }

    GIVEN("a response with no Content-Type") {
        HttpResponse response;
        response.status_code = 200;

        THEN("both content type checks return false") {
            REQUIRE(response.is_sse() == false);
            REQUIRE(response.is_json() == false);
        }
    }
}


