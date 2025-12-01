#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mcpp {

/// Represents a single Server-Sent Event parsed from an SSE stream.
///
/// SSE format (https://html.spec.whatwg.org/multipage/server-sent-events.html):
///   event: <event-type>     (optional, defaults to "message")
///   id: <event-id>          (optional, used for resumability)
///   data: <payload>         (the actual content)
///   retry: <milliseconds>   (optional, reconnection time hint)
///   <blank line>            (signals end of event)
///
struct SseEvent {
    std::optional<std::string> id;      // For Last-Event-ID resumption
    std::optional<std::string> event;   // Event type (e.g., "message")
    std::string data;                   // Concatenated data lines
    std::optional<std::uint32_t> retry; // Reconnection time in milliseconds (server hint)
};

/// Exception thrown when SSE parser buffer limit is exceeded
class SseBufferOverflowError : public std::runtime_error {
public:
    explicit SseBufferOverflowError(std::size_t size, std::size_t limit)
        : std::runtime_error("SSE buffer overflow: " + std::to_string(size) + 
                            " bytes exceeds limit of " + std::to_string(limit))
        , buffer_size(size)
        , buffer_limit(limit)
    {}
    
    std::size_t buffer_size;
    std::size_t buffer_limit;
};

/// Configuration for SSE parser
struct SseParserConfig {
    /// Maximum buffer size before throwing SseBufferOverflowError
    /// Default 1MB is generous for typical SSE events
    std::size_t max_buffer_size{1024 * 1024};
    
    /// Maximum size for a single event's data field
    /// Default 512KB
    std::size_t max_event_size{512 * 1024};
};

/// Incremental parser for Server-Sent Events.
///
/// SSE data may arrive in arbitrary chunks over the network. This parser
/// buffers partial lines and emits complete events as they become available.
///
/// Usage:
///   SseParser parser;
///   for (auto& chunk : incoming_data) {
///       auto events = parser.feed(chunk);
///       for (auto& event : events) {
///           process(event);
///       }
///   }
///
class SseParser {
public:
    SseParser() = default;
    explicit SseParser(SseParserConfig config) : config_(config) {}

    /// Feed a chunk of data to the parser.
    /// Returns any complete events that were parsed.
    /// Partial data is buffered internally until the next call.
    /// Throws SseBufferOverflowError if buffer exceeds max_buffer_size.
    [[nodiscard]] std::vector<SseEvent> feed(std::string_view chunk);

    /// Reset the parser state, discarding any buffered data.
    void reset();
    
    /// Get current buffer size (for monitoring)
    [[nodiscard]] std::size_t buffer_size() const noexcept { return buffer_.size(); }
    
    /// Get configuration
    [[nodiscard]] const SseParserConfig& config() const noexcept { return config_; }

private:
    SseParserConfig config_;
    std::string buffer_;           // Accumulates incoming data
    std::size_t buffer_pos_{0};    // Current read position (avoids O(n) erase)
    std::string current_data_;     // Data lines for current event
    std::optional<std::string> current_id_;
    std::optional<std::string> current_event_;
    std::optional<std::uint32_t> current_retry_;  // Retry hint from server
    
    /// Compact buffer if consumed portion exceeds threshold
    void maybe_compact_buffer();

    /// Process a single complete line (without trailing newline).
    /// Returns true if a complete event is ready (blank line was seen).
    bool process_line(std::string_view line);

    /// Build and return the current event, then reset current_* fields.
    SseEvent emit_event();
};

}  // namespace mcpp

