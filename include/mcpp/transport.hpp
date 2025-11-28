#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// Transport Common Types
// ═══════════════════════════════════════════════════════════════════════════
// Shared types used by all transport implementations.
//
// For the full HTTP transport, use: #include "mcpp/transport/http_transport.hpp"
// For process transport, use: #include "mcpp/transport/process_transport.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include <tl/expected.hpp>

namespace mcpp {

using Json = nlohmann::json;
using HeaderMap = std::unordered_map<std::string, std::string>;

/// Error type for transport operations
struct TransportError {
    enum class Category { Network, Timeout, Protocol };

    Category category{};
    std::string message;
    std::optional<int> status_code{};
};

/// Result type for transport operations
template <typename T>
using TransportResult = tl::expected<T, TransportError>;

}  // namespace mcpp

