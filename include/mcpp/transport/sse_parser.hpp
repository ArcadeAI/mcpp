#pragma once

#include <cstdint>
#include <optional>
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
    /// Feed a chunk of data to the parser.
    /// Returns any complete events that were parsed.
    /// Partial data is buffered internally until the next call.
    [[nodiscard]] std::vector<SseEvent> feed(std::string_view chunk);

    /// Reset the parser state, discarding any buffered data.
    void reset();

private:
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

