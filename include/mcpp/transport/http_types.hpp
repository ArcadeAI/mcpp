#pragma once

#include <ada.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// Case-Insensitive Header Lookup
// ─────────────────────────────────────────────────────────────────────────────
// HTTP header names are case-insensitive per RFC 7230.
// This utility provides safe lookup regardless of casing.

using HeaderMap = std::unordered_map<std::string, std::string>;

/// Find a header by name (case-insensitive).
/// Returns iterator to the found header, or headers.end() if not found.
inline HeaderMap::const_iterator find_header(
    const HeaderMap& headers,
    std::string_view name
) {
    return std::ranges::find_if(headers,
        [&name](const auto& pair) {
            const auto& key = pair.first;
            return key.size() == name.size() &&
                   std::ranges::equal(key, name,
                       [](char a, char b) {
                           return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b));
                       });
        });
}

/// Get header value by name (case-insensitive).
/// Returns std::nullopt if header not found.
inline std::optional<std::string> get_header(
    const HeaderMap& headers,
    std::string_view name
) {
    const auto it = find_header(headers, name);
    const bool found = (it != headers.end());
    if (found) {
        return it->second;
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP Method Enum
// ─────────────────────────────────────────────────────────────────────────────
// MCP uses: POST (send messages), GET (open SSE stream), DELETE (close session)

enum class HttpMethod {
    Get,
    Post,
    Delete
};

// Convert enum to string for HTTP request line
inline std::string to_string(HttpMethod method) {
    switch (method) {
        case HttpMethod::Get:    return "GET";
        case HttpMethod::Post:   return "POST";
        case HttpMethod::Delete: return "DELETE";
    }
    return "UNKNOWN";  // Should never reach here
}

// ─────────────────────────────────────────────────────────────────────────────
// Header Map Type
// ─────────────────────────────────────────────────────────────────────────────
// HttpRequest
// ─────────────────────────────────────────────────────────────────────────────
// Represents an outgoing HTTP request.

struct HttpRequest {
    HttpMethod method{HttpMethod::Get};
    std::string path;           // e.g., "/mcp" or "/mcp/sse"
    HeaderMap headers;
    std::optional<std::string> body;  // Only for POST

    // Builder-style helpers
    HttpRequest& with_header(const std::string& name, const std::string& value) {
        headers[name] = value;
        return *this;
    }

    HttpRequest& with_body(const std::string& content) {
        body = content;
        return *this;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// HttpResponse
// ─────────────────────────────────────────────────────────────────────────────
// Represents an incoming HTTP response (headers only — body handled separately).

struct HttpResponse {
    std::uint16_t status_code{0};
    std::string status_message;  // e.g., "OK", "Not Found"
    HeaderMap headers;

    // Helper to check status code ranges
    [[nodiscard]] bool is_success() const {
        return (status_code >= 200) && (status_code < 300);
    }

    [[nodiscard]] bool is_redirect() const {
        return (status_code >= 300) && (status_code < 400);
    }

    [[nodiscard]] bool is_client_error() const {
        return (status_code >= 400) && (status_code < 500);
    }

    [[nodiscard]] bool is_server_error() const {
        return (status_code >= 500) && (status_code < 600);
    }

    // Get header value (case-sensitive lookup)
    [[nodiscard]] std::optional<std::string> get_header(const std::string& name) const {
        const auto it = headers.find(name);
        const bool found = (it != headers.end());
        if (found) {
            return it->second;
        }
        return std::nullopt;
    }

    // Check Content-Type
    [[nodiscard]] bool is_sse() const {
        const auto content_type = get_header("Content-Type");
        const bool has_content_type = content_type.has_value();
        if (has_content_type == false) {
            return false;
        }
        // SSE content type: "text/event-stream" (may include charset)
        return content_type->find("text/event-stream") != std::string::npos;
    }

    [[nodiscard]] bool is_json() const {
        const auto content_type = get_header("Content-Type");
        const bool has_content_type = content_type.has_value();
        if (has_content_type == false) {
            return false;
        }
        return content_type->find("application/json") != std::string::npos;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// URL Components
// ─────────────────────────────────────────────────────────────────────────────
// Parsed URL for connecting to HTTP servers.
// Uses ada-url (WHATWG-compliant URL parser used by Node.js).

struct UrlComponents {
    std::string scheme;   // "http" or "https"
    std::string host;     // "api.example.com"
    std::uint16_t port;   // 443 for https, 80 for http (or explicit)
    std::string path;     // "/mcp" (includes leading slash)
    std::string query;    // "?foo=bar" (optional, includes ?)

    [[nodiscard]] bool is_secure() const {
        return scheme == "https";
    }

    [[nodiscard]] std::string host_with_port() const {
        return host + ":" + std::to_string(port);
    }

    [[nodiscard]] std::string path_with_query() const {
        const bool has_query = (query.empty() == false);
        if (has_query) {
            return path + query;
        }
        return path;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// URL Parsing (using ada-url)
// ─────────────────────────────────────────────────────────────────────────────
// Parse a URL string into components using the ada-url library.
// Returns nullopt on invalid URL.
//
// ada-url is:
// - WHATWG URL Standard compliant
// - Used by Node.js and Cloudflare Workers
// - One of the fastest URL parsers available
// - Handles edge cases, IDN, percent encoding, etc.

std::optional<UrlComponents> parse_url(const std::string& url);

}  // namespace mcpp

