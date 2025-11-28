// ─────────────────────────────────────────────────────────────────────────────
// URL Validator Tests
// ─────────────────────────────────────────────────────────────────────────────
// Tests for security-focused URL validation used in elicitation URL mode.

#include <catch2/catch_test_macros.hpp>

#include "mcpp/security/url_validator.hpp"

using namespace mcpp::security;

// ═══════════════════════════════════════════════════════════════════════════
// Basic Validation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Valid HTTPS URL passes validation", "[security][url]") {
    auto result = validate_url("https://example.com/auth");
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.display_domain == "example.com");
    REQUIRE_FALSE(result.error.has_value());
}

TEST_CASE("Valid HTTPS URL with path and query", "[security][url]") {
    auto result = validate_url("https://auth.example.com/oauth/callback?state=abc123");
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.display_domain == "auth.example.com");
}

TEST_CASE("Valid HTTPS URL with port", "[security][url]") {
    auto result = validate_url("https://example.com:8443/secure");
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.display_domain == "example.com:8443");
    REQUIRE(result.warning.has_value());  // Non-standard port warning
}

// ═══════════════════════════════════════════════════════════════════════════
// Protocol Validation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("HTTP URL rejected by default", "[security][url]") {
    auto result = validate_url("http://example.com/auth");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
    REQUIRE(result.error.has_value());
    REQUIRE(result.error->find("HTTPS") != std::string::npos);
}

TEST_CASE("HTTP URL allowed when configured", "[security][url]") {
    UrlValidationConfig config;
    config.allow_http = true;
    
    auto result = validate_url("http://example.com/auth", config);
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.warning.has_value());  // Warning about HTTP
}

TEST_CASE("FTP URL rejected", "[security][url]") {
    auto result = validate_url("ftp://example.com/file");
    
    REQUIRE_FALSE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("JavaScript URL rejected", "[security][url]") {
    auto result = validate_url("javascript:alert(1)");
    
    REQUIRE_FALSE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("Data URL rejected", "[security][url]") {
    auto result = validate_url("data:text/html,<script>alert(1)</script>");
    
    REQUIRE_FALSE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

// ═══════════════════════════════════════════════════════════════════════════
// Localhost Blocking Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("localhost rejected", "[security][url][localhost]") {
    auto result = validate_url("https://localhost/admin");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
    REQUIRE(result.error.has_value());
    REQUIRE(result.error->find("localhost") != std::string::npos);
}

TEST_CASE("127.0.0.1 rejected", "[security][url][localhost]") {
    auto result = validate_url("https://127.0.0.1/admin");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("127.0.0.1 with port rejected", "[security][url][localhost]") {
    auto result = validate_url("https://127.0.0.1:8080/admin");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("IPv6 localhost [::1] rejected", "[security][url][localhost]") {
    auto result = validate_url("https://[::1]/admin");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("0.0.0.0 rejected", "[security][url][localhost]") {
    auto result = validate_url("https://0.0.0.0/");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("localhost allowed when configured", "[security][url][localhost]") {
    UrlValidationConfig config;
    config.allow_localhost = true;
    
    auto result = validate_url("https://localhost/admin", config);
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.warning.has_value());  // Warning about localhost
}

// ═══════════════════════════════════════════════════════════════════════════
// Private IP Range Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("10.x.x.x private range rejected", "[security][url][private]") {
    auto result = validate_url("https://10.0.0.1/internal");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
    REQUIRE(result.error.has_value());
}

TEST_CASE("172.16.x.x private range rejected", "[security][url][private]") {
    auto result = validate_url("https://172.16.0.1/internal");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("172.31.x.x private range rejected", "[security][url][private]") {
    auto result = validate_url("https://172.31.255.255/internal");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("172.32.x.x NOT private (edge case)", "[security][url][private]") {
    auto result = validate_url("https://172.32.0.1/");
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);  // 172.32 is public
}

TEST_CASE("192.168.x.x private range rejected", "[security][url][private]") {
    auto result = validate_url("https://192.168.1.1/router");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("169.254.x.x link-local rejected", "[security][url][private]") {
    auto result = validate_url("https://169.254.1.1/");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("AWS metadata endpoint rejected", "[security][url][private]") {
    auto result = validate_url("https://169.254.169.254/latest/meta-data/");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
    REQUIRE(result.error.has_value());
    // Error should mention either "metadata" or "link-local"
    bool mentions_issue = result.error->find("metadata") != std::string::npos || 
                          result.error->find("link-local") != std::string::npos;
    REQUIRE(mentions_issue);
}

TEST_CASE("Private IPs allowed when configured", "[security][url][private]") {
    UrlValidationConfig config;
    config.allow_private_ips = true;
    
    auto result = validate_url("https://192.168.1.1/router", config);
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.warning.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// IP Address Warning Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Public IP address gives warning", "[security][url][ip]") {
    auto result = validate_url("https://8.8.8.8/dns");
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);  // Public IP is safe
    REQUIRE(result.warning.has_value());  // But warn about IP instead of domain
}

TEST_CASE("IP addresses blocked when configured", "[security][url][ip]") {
    UrlValidationConfig config;
    config.allow_ip_addresses = false;
    
    auto result = validate_url("https://8.8.8.8/dns", config);
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

// ═══════════════════════════════════════════════════════════════════════════
// Domain Display Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Display domain extracts correctly", "[security][url][display]") {
    SECTION("Simple domain") {
        auto result = validate_url("https://example.com/path");
        REQUIRE(result.display_domain == "example.com");
    }
    
    SECTION("Subdomain") {
        auto result = validate_url("https://auth.api.example.com/oauth");
        REQUIRE(result.display_domain == "auth.api.example.com");
    }
    
    SECTION("With non-standard port") {
        auto result = validate_url("https://example.com:8443/");
        REQUIRE(result.display_domain == "example.com:8443");
    }
    
    SECTION("Standard HTTPS port not shown") {
        auto result = validate_url("https://example.com:443/");
        REQUIRE(result.display_domain == "example.com");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Whitelist/Blacklist Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Allowed hosts whitelist", "[security][url][whitelist]") {
    UrlValidationConfig config;
    config.allowed_hosts = {"trusted.com", "auth.trusted.com"};
    
    SECTION("Whitelisted host passes") {
        auto result = validate_url("https://trusted.com/auth", config);
        REQUIRE(result.is_safe);
    }
    
    SECTION("Non-whitelisted host blocked") {
        auto result = validate_url("https://untrusted.com/auth", config);
        REQUIRE_FALSE(result.is_safe);
        REQUIRE(result.error.has_value());
    }
}

TEST_CASE("Blocked hosts blacklist", "[security][url][blacklist]") {
    UrlValidationConfig config;
    config.blocked_hosts = {"evil.com", "phishing.example.com"};
    
    SECTION("Blacklisted host blocked") {
        auto result = validate_url("https://evil.com/steal", config);
        REQUIRE_FALSE(result.is_safe);
    }
    
    SECTION("Non-blacklisted host passes") {
        auto result = validate_url("https://good.com/auth", config);
        REQUIRE(result.is_safe);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Empty URL rejected", "[security][url][edge]") {
    auto result = validate_url("");
    
    REQUIRE_FALSE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("Malformed URL rejected", "[security][url][edge]") {
    auto result = validate_url("not a url");
    
    REQUIRE_FALSE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
}

TEST_CASE("URL with credentials rejected", "[security][url][edge]") {
    auto result = validate_url("https://user:pass@example.com/");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);
    REQUIRE(result.error.has_value());
    REQUIRE(result.error->find("credentials") != std::string::npos);
}

TEST_CASE("Very long URL gives warning", "[security][url][edge]") {
    std::string long_url = "https://example.com/" + std::string(2100, 'a');  // > 2048 default
    auto result = validate_url(long_url);
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.warning.has_value());
}

TEST_CASE("Deep subdomain gives warning", "[security][url][edge]") {
    auto result = validate_url("https://a.b.c.d.e.f.example.com/");
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);
    REQUIRE(result.warning.has_value());  // Unusual subdomain depth
}

// ═══════════════════════════════════════════════════════════════════════════
// Security Edge Cases - Bypass Attempts
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Decimal IP representation blocked", "[security][url][bypass]") {
    // 2130706433 = 127.0.0.1 in decimal
    // ada-url normalizes this to 127.0.0.1
    auto result = validate_url("https://2130706433/admin");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);  // Should be blocked as localhost
}

TEST_CASE("Octal IP representation blocked", "[security][url][bypass]") {
    // 0177.0.0.1 = 127.0.0.1 in octal
    auto result = validate_url("https://0177.0.0.1/admin");
    
    // Note: ada-url may or may not normalize this
    // If valid, should be blocked
    if (result.is_valid) {
        REQUIRE_FALSE(result.is_safe);
    }
}

TEST_CASE("Hex IP representation blocked", "[security][url][bypass]") {
    // 0x7f000001 = 127.0.0.1 in hex
    auto result = validate_url("https://0x7f000001/admin");
    
    // Note: ada-url may or may not normalize this
    if (result.is_valid) {
        REQUIRE_FALSE(result.is_safe);
    }
}

TEST_CASE("Case insensitive host matching in whitelist", "[security][url][whitelist]") {
    UrlValidationConfig config;
    config.allowed_hosts = {"trusted.com"};
    
    auto result = validate_url("https://TRUSTED.COM/auth", config);
    
    REQUIRE(result.is_valid);
    REQUIRE(result.is_safe);  // Should match case-insensitively
}

TEST_CASE("Case insensitive host matching in blacklist", "[security][url][blacklist]") {
    UrlValidationConfig config;
    config.blocked_hosts = {"evil.com"};
    
    auto result = validate_url("https://EVIL.COM/steal", config);
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);  // Should block case-insensitively
}

TEST_CASE("IPv6 link-local addresses blocked", "[security][url][private]") {
    auto result = validate_url("https://[fe80::1]/admin");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);  // fe80::/10 is link-local
}

TEST_CASE("IPv6 private addresses blocked", "[security][url][private]") {
    auto result = validate_url("https://[fc00::1]/internal");
    
    REQUIRE(result.is_valid);
    REQUIRE_FALSE(result.is_safe);  // fc00::/7 is private
}

