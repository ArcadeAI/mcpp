#include "mcpp/transport/http_types.hpp"

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// URL Parser Implementation (using ada-url)
// ─────────────────────────────────────────────────────────────────────────────
// ada-url is a fast, WHATWG-compliant URL parser used by Node.js.
// It handles all the edge cases we don't want to deal with manually:
// - IDN (internationalized domain names)
// - Percent encoding/decoding
// - IPv6 addresses
// - Default ports
// - Normalization
//
// See: https://github.com/ada-url/ada

std::optional<UrlComponents> parse_url(const std::string& url) {
    // Parse URL using ada
    auto parsed = ada::parse<ada::url>(url);

    // Check if parsing succeeded
    const bool parse_failed = (parsed.has_value() == false);
    if (parse_failed) {
        return std::nullopt;
    }

    const auto& ada_url = parsed.value();

    // Extract scheme (without trailing colon)
    std::string scheme = std::string(ada_url.get_protocol());
    // ada returns "https:" - remove the colon
    const bool has_colon = (scheme.empty() == false) && (scheme.back() == ':');
    if (has_colon) {
        scheme.pop_back();
    }

    // Only allow http/https
    const bool is_http = (scheme == "http");
    const bool is_https = (scheme == "https");
    const bool valid_scheme = is_http || is_https;
    if (valid_scheme == false) {
        return std::nullopt;
    }

    // Extract host
    std::string host = std::string(ada_url.get_hostname());
    const bool empty_host = host.empty();
    if (empty_host) {
        return std::nullopt;
    }

    // Extract port (ada handles defaults)
    std::uint16_t port = 0;
    const auto port_str = ada_url.get_port();
    const bool has_explicit_port = (port_str.empty() == false);
    if (has_explicit_port) {
        port = static_cast<std::uint16_t>(std::stoi(std::string(port_str)));
    } else {
        // Use default port based on scheme
        port = is_https ? 443 : 80;
    }

    // Extract path (ada returns "/" for empty path)
    std::string path = std::string(ada_url.get_pathname());
    const bool empty_path = path.empty();
    if (empty_path) {
        path = "/";
    }

    // Extract query (includes "?" if present)
    std::string query = std::string(ada_url.get_search());

    // Build result
    UrlComponents result;
    result.scheme = std::move(scheme);
    result.host = std::move(host);
    result.port = port;
    result.path = std::move(path);
    result.query = std::move(query);

    return result;
}

}  // namespace mcpp
