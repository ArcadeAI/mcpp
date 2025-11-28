#include "mcpp/security/url_validator.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace mcpp::security {

namespace detail {

// ═══════════════════════════════════════════════════════════════════════════
// IPv4 Parsing
// ═══════════════════════════════════════════════════════════════════════════

bool parse_ipv4(std::string_view host, std::array<std::uint8_t, 4>& octets) {
    // Simple IPv4 parser: a.b.c.d where each is 0-255
    std::size_t pos = 0;
    
    for (int i = 0; i < 4; ++i) {
        if (pos >= host.size()) return false;
        
        // Find end of this octet
        std::size_t end = host.find('.', pos);
        if (i < 3 && end == std::string_view::npos) return false;
        if (i == 3 && end != std::string_view::npos) return false;
        if (i == 3) end = host.size();
        
        // Parse octet
        std::string_view octet_str = host.substr(pos, end - pos);
        if (octet_str.empty() || octet_str.size() > 3) return false;
        
        // Check all digits
        for (char c : octet_str) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        
        int value = 0;
        auto [ptr, ec] = std::from_chars(octet_str.data(), octet_str.data() + octet_str.size(), value);
        if (ec != std::errc{} || value < 0 || value > 255) return false;
        
        octets[i] = static_cast<std::uint8_t>(value);
        pos = end + 1;
    }
    
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Host Classification
// ═══════════════════════════════════════════════════════════════════════════

bool is_ip_address(std::string_view host) {
    // Check for IPv6 (starts with [ or contains ::)
    if (host.starts_with('[') || host.find("::") != std::string_view::npos) {
        return true;
    }
    
    // Check for IPv4
    std::array<std::uint8_t, 4> octets;
    return parse_ipv4(host, octets);
}

bool is_localhost(std::string_view host) {
    // Remove brackets from IPv6
    if (host.starts_with('[') && host.ends_with(']')) {
        host = host.substr(1, host.size() - 2);
    }
    
    // Check common localhost names
    if (host == "localhost" || host == "localhost.localdomain") {
        return true;
    }
    
    // Check IPv6 loopback
    if (host == "::1" || host == "0:0:0:0:0:0:0:1") {
        return true;
    }
    
    // Check IPv4 loopback (127.x.x.x)
    std::array<std::uint8_t, 4> octets;
    if (parse_ipv4(host, octets)) {
        if (octets[0] == 127) return true;
        if (octets[0] == 0 && octets[1] == 0 && octets[2] == 0 && octets[3] == 0) return true;
    }
    
    return false;
}

bool is_private_ip(std::string_view host) {
    std::array<std::uint8_t, 4> octets;
    if (!parse_ipv4(host, octets)) {
        return false;  // Not an IPv4 address
    }
    
    // 10.0.0.0/8 (10.x.x.x)
    if (octets[0] == 10) return true;
    
    // 172.16.0.0/12 (172.16.x.x - 172.31.x.x)
    if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) return true;
    
    // 192.168.0.0/16 (192.168.x.x)
    if (octets[0] == 192 && octets[1] == 168) return true;
    
    return false;
}

bool is_link_local_ip(std::string_view host) {
    // Check IPv4 link-local
    std::array<std::uint8_t, 4> octets;
    if (parse_ipv4(host, octets)) {
        // 169.254.0.0/16 (link-local, includes AWS metadata endpoint)
        return octets[0] == 169 && octets[1] == 254;
    }
    
    // Check IPv6 link-local
    return is_ipv6_link_local(host);
}

bool is_ipv6_link_local(std::string_view host) {
    // Remove brackets if present
    if (host.starts_with('[') && host.ends_with(']')) {
        host = host.substr(1, host.size() - 2);
    }
    
    // fe80::/10 - link-local addresses start with fe80-febf
    // Simplified check: starts with "fe8", "fe9", "fea", "feb" (case-insensitive)
    if (host.size() < 4) return false;
    
    // Convert first 3 chars to lowercase for comparison
    char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(host[0])));
    char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(host[1])));
    char c2 = static_cast<char>(std::tolower(static_cast<unsigned char>(host[2])));
    
    if (c0 != 'f' || c1 != 'e') return false;
    if (c2 != '8' && c2 != '9' && c2 != 'a' && c2 != 'b') return false;
    
    return true;
}

bool is_ipv6_private(std::string_view host) {
    // Remove brackets if present
    if (host.starts_with('[') && host.ends_with(']')) {
        host = host.substr(1, host.size() - 2);
    }
    
    // fc00::/7 - unique local addresses start with fc or fd
    if (host.size() < 2) return false;
    
    char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(host[0])));
    char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(host[1])));
    
    return c0 == 'f' && (c1 == 'c' || c1 == 'd');
}

std::size_t count_subdomains(std::string_view host) {
    if (host.empty()) return 0;
    
    std::size_t count = 1;
    for (char c : host) {
        if (c == '.') ++count;
    }
    return count;
}

}  // namespace detail

// ═══════════════════════════════════════════════════════════════════════════
// Main Validation Function
// ═══════════════════════════════════════════════════════════════════════════

UrlValidationResult validate_url(const std::string& url, const UrlValidationConfig& config) {
    UrlValidationResult result;
    
    // Empty URL check
    if (url.empty()) {
        result.error = "URL is empty";
        return result;
    }
    
    // Parse URL using ada
    auto parsed = ada::parse<ada::url>(url);
    if (!parsed.has_value()) {
        result.error = "Invalid URL format";
        return result;
    }
    
    const auto& ada_url = parsed.value();
    result.is_valid = true;
    result.normalized_url = std::string(ada_url.get_href());
    
    // Extract scheme (without colon)
    std::string scheme = std::string(ada_url.get_protocol());
    if (!scheme.empty() && scheme.back() == ':') {
        scheme.pop_back();
    }
    
    // Protocol validation
    bool is_https = (scheme == "https");
    bool is_http = (scheme == "http");
    
    if (!is_https && !is_http) {
        result.is_valid = false;  // Non-HTTP schemes are invalid for our purposes
        result.error = "Only HTTP/HTTPS URLs are allowed";
        return result;
    }
    
    if (!is_https && !config.allow_http) {
        result.error = "Only HTTPS URLs are allowed for security";
        return result;
    }
    
    if (is_http && config.allow_http) {
        result.warning = "HTTP connection is not encrypted";
    }
    
    // Extract host
    std::string host = std::string(ada_url.get_hostname());
    if (host.empty()) {
        result.error = "URL has no host";
        return result;
    }
    
    // Check for credentials in URL (always blocked)
    std::string username = std::string(ada_url.get_username());
    std::string password = std::string(ada_url.get_password());
    if (!username.empty() || !password.empty()) {
        result.error = "URLs with embedded credentials are not allowed";
        return result;
    }
    
    // Build display domain
    std::string port_str = std::string(ada_url.get_port());
    bool has_non_standard_port = !port_str.empty() && 
        !((is_https && port_str == "443") || (is_http && port_str == "80"));
    
    if (has_non_standard_port) {
        result.display_domain = host + ":" + port_str;
        if (!result.warning) {
            result.warning = "URL uses non-standard port: " + port_str;
        }
    } else {
        result.display_domain = host;
    }
    
    // Localhost check
    if (detail::is_localhost(host)) {
        if (!config.allow_localhost) {
            result.error = "localhost URLs are blocked for security";
            return result;
        }
        if (!result.warning) {
            result.warning = "URL points to localhost";
        }
    }
    
    // Private IP check
    if (detail::is_private_ip(host)) {
        if (!config.allow_private_ips) {
            result.error = "Private IP addresses are blocked for security";
            return result;
        }
        if (!result.warning) {
            result.warning = "URL points to private network";
        }
    }
    
    // Link-local check (includes AWS metadata endpoint)
    if (detail::is_link_local_ip(host)) {
        if (!config.allow_private_ips) {
            result.error = "Link-local addresses (including cloud metadata endpoints) are blocked";
            return result;
        }
        if (!result.warning) {
            result.warning = "URL points to link-local address";
        }
    }
    
    // IPv6 private address check (fc00::/7)
    if (detail::is_ipv6_private(host)) {
        if (!config.allow_private_ips) {
            result.error = "Private IPv6 addresses are blocked for security";
            return result;
        }
        if (!result.warning) {
            result.warning = "URL points to private IPv6 network";
        }
    }
    
    // IP address warning (only for public IPs)
    if (detail::is_ip_address(host) && !detail::is_localhost(host) && 
        !detail::is_private_ip(host) && !detail::is_link_local_ip(host) &&
        !detail::is_ipv6_private(host)) {
        if (!config.allow_ip_addresses) {
            result.error = "IP addresses are not allowed, use domain names";
            return result;
        }
        if (!result.warning) {
            result.warning = "URL uses IP address instead of domain name";
        }
    }
    
    // Whitelist check
    if (!config.allowed_hosts.empty()) {
        bool found = std::find(config.allowed_hosts.begin(), config.allowed_hosts.end(), host) 
                     != config.allowed_hosts.end();
        if (!found) {
            result.error = "Host '" + host + "' is not in the allowed hosts list";
            return result;
        }
    }
    
    // Blacklist check
    if (!config.blocked_hosts.empty()) {
        bool found = std::find(config.blocked_hosts.begin(), config.blocked_hosts.end(), host)
                     != config.blocked_hosts.end();
        if (found) {
            result.error = "Host '" + host + "' is blocked";
            return result;
        }
    }
    
    // URL length warning
    if (url.size() > config.max_url_length) {
        if (!result.warning) {
            result.warning = "URL is unusually long";
        }
    }
    
    // Subdomain depth warning
    std::size_t depth = detail::count_subdomains(host);
    if (depth > config.max_subdomain_depth) {
        if (!result.warning) {
            result.warning = "URL has unusually deep subdomain structure";
        }
    }
    
    // All checks passed
    result.is_safe = true;
    return result;
}

}  // namespace mcpp::security

