#ifndef MCPP_SECURITY_URL_VALIDATOR_HPP
#define MCPP_SECURITY_URL_VALIDATOR_HPP

#include <ada.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp::security {

// ═══════════════════════════════════════════════════════════════════════════
// URL Validation Configuration
// ═══════════════════════════════════════════════════════════════════════════
// Configure which URLs are considered safe for elicitation URL mode.

struct UrlValidationConfig {
    bool allow_http = false;           // Default: HTTPS only
    bool allow_localhost = false;      // Default: block localhost
    bool allow_private_ips = false;    // Default: block private ranges
    bool allow_ip_addresses = true;    // Default: allow public IPs (with warning)
    
    std::vector<std::string> allowed_hosts;  // Whitelist (empty = allow all)
    std::vector<std::string> blocked_hosts;  // Blacklist
    
    std::size_t max_url_length = 2048;       // Warn if URL exceeds this
    std::size_t max_subdomain_depth = 5;     // Warn if deeper than this
};

// ═══════════════════════════════════════════════════════════════════════════
// URL Validation Result
// ═══════════════════════════════════════════════════════════════════════════
// Result of URL validation with detailed information for user consent.

struct UrlValidationResult {
    bool is_valid{false};              // URL is well-formed
    bool is_safe{false};               // URL passes security checks
    std::string display_domain;        // Human-readable domain for consent UI
    std::string normalized_url;        // Normalized URL for opening
    std::optional<std::string> warning;  // Non-blocking concern
    std::optional<std::string> error;    // Blocking security issue
};

// ═══════════════════════════════════════════════════════════════════════════
// URL Validation Function
// ═══════════════════════════════════════════════════════════════════════════
// Validate a URL for use in elicitation URL mode.
//
// Security checks performed:
// - Protocol: HTTPS required (unless allow_http)
// - Localhost: Blocked (unless allow_localhost)
// - Private IPs: Blocked (unless allow_private_ips)
// - Credentials: Always blocked (user:pass@host)
// - Whitelist/Blacklist: Applied if configured
//
// Returns UrlValidationResult with:
// - is_valid: true if URL is well-formed
// - is_safe: true if URL passes all security checks
// - display_domain: Domain to show user for consent
// - warning: Optional non-blocking concern
// - error: Optional blocking security issue
//
// ═══════════════════════════════════════════════════════════════════════════
// SECURITY LIMITATIONS
// ═══════════════════════════════════════════════════════════════════════════
//
// DNS Rebinding: This validator only checks the hostname at validation time.
// A malicious DNS server could initially resolve a hostname to a public IP
// (passing validation), then rebind to 127.0.0.1 or a private IP when the
// actual HTTP request is made. Mitigations:
//   - Use certificate pinning for sensitive endpoints
//   - Implement resolved IP validation in the HTTP client layer
//   - Use a DNS resolver that caches and pins results
//
// Time-of-check to time-of-use (TOCTOU): The URL could change between
// validation and actual use. Always validate immediately before use.

[[nodiscard]] UrlValidationResult validate_url(
    const std::string& url,
    const UrlValidationConfig& config = {}
);

// ═══════════════════════════════════════════════════════════════════════════
// Internal Helpers (exposed for testing)
// ═══════════════════════════════════════════════════════════════════════════

namespace detail {

// Check if host is localhost or loopback
[[nodiscard]] bool is_localhost(std::string_view host);

// Check if IP is in private range (RFC 1918)
[[nodiscard]] bool is_private_ip(std::string_view host);

// Check if IP is link-local (169.254.x.x for IPv4, fe80::/10 for IPv6)
[[nodiscard]] bool is_link_local_ip(std::string_view host);

// Check if IPv6 address is private (fc00::/7)
[[nodiscard]] bool is_ipv6_private(std::string_view host);

// Check if IPv6 address is link-local (fe80::/10)
[[nodiscard]] bool is_ipv6_link_local(std::string_view host);

// Check if string looks like an IP address
[[nodiscard]] bool is_ip_address(std::string_view host);

// Count subdomain depth
[[nodiscard]] std::size_t count_subdomains(std::string_view host);

// Parse IPv4 octets (returns false if not valid IPv4)
[[nodiscard]] bool parse_ipv4(std::string_view host, std::array<std::uint8_t, 4>& octets);

}  // namespace detail

}  // namespace mcpp::security

#endif  // MCPP_SECURITY_URL_VALIDATOR_HPP

