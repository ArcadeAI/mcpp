#include "mcpp/transport/sse_parser.hpp"

#include <charconv>

namespace mcpp {

// Compact buffer when consumed portion exceeds this threshold (4KB)
// This avoids O(n) erase on every line while still bounding memory waste
constexpr std::size_t buffer_compact_threshold = 4096;

std::vector<SseEvent> SseParser::feed(std::string_view chunk) {
    std::vector<SseEvent> events;

    // Check if adding this chunk would exceed buffer limit
    const std::size_t new_size = buffer_.size() + chunk.size();
    if (new_size > config_.max_buffer_size) {
        throw SseBufferOverflowError(new_size, config_.max_buffer_size);
    }

    buffer_ += chunk;

    // Search for newlines starting from current position
    std::size_t newline_pos = 0;
    while ((newline_pos = buffer_.find('\n', buffer_pos_)) != std::string::npos) {
        // Extract the line (from buffer_pos_ to newline)
        std::string_view line_view(buffer_.data() + buffer_pos_, newline_pos - buffer_pos_);

        // Handle CRLF: if line ends with '\r', exclude it
        const bool has_carriage_return = 
            (!line_view.empty()) && (line_view.back() == '\r');
        if (has_carriage_return) {
            line_view.remove_suffix(1);
        }

        // Advance position past the newline
        buffer_pos_ = newline_pos + 1;

        // Process the line. Returns true if this was a blank line (event complete)
        const bool event_complete = process_line(line_view);
        if (event_complete) {
            // Check event size limit before emitting
            if (current_data_.size() > config_.max_event_size) {
                // Discard oversized event and reset state
                current_data_.clear();
                current_id_ = std::nullopt;
                current_event_ = std::nullopt;
                current_retry_ = std::nullopt;
                // Don't throw - just skip this event
                continue;
            }
            events.push_back(emit_event());
        }
    }

    // Compact buffer if we've consumed a lot
    maybe_compact_buffer();

    return events;
}

void SseParser::maybe_compact_buffer() {
    if (buffer_pos_ > buffer_compact_threshold) {
        buffer_.erase(0, buffer_pos_);
        buffer_pos_ = 0;
    }
}

void SseParser::reset() {
    buffer_.clear();
    buffer_pos_ = 0;
    current_data_.clear();
    current_id_ = std::nullopt;
    current_event_ = std::nullopt;
    current_retry_ = std::nullopt;
}

bool SseParser::process_line(std::string_view line) {
    if (line.empty()) {
        return true;  // Event is complete!
    }

    const bool is_comment = (line.front() == ':');
    if (is_comment) {
        return false;  // Ignore comment, event not complete
    }

    std::string_view field_name;
    std::string_view field_value;

    const std::size_t colon_pos = line.find(':');
    const bool has_colon = (colon_pos != std::string_view::npos);

    if (has_colon == false) {
        // No colon: entire line is field name, value is empty
        // This is rare but valid per spec
        field_name = line;
        field_value = "";
    } else {
        // Split at colon
        field_name = line.substr(0, colon_pos);
        
        // Value is everything after the colon
        std::size_t value_start = colon_pos + 1;
        
        // Skip optional space after colon (per SSE spec)
        const bool has_space_after_colon =
            (value_start < line.size()) && (line[value_start] == ' ');
        if (has_space_after_colon) {
            value_start += 1;
        }
        
        field_value = line.substr(value_start);
    }

    if (field_name == "event") {
        current_event_ = std::string(field_value);
    }
    else if (field_name == "id") {
        current_id_ = std::string(field_value);
    }
    else if (field_name == "data") {
        // Multiple data lines are concatenated with newlines between them
        // Example:
        //   data: line one
        //   data: line two
        // Becomes: "line one\nline two"
        const bool data_already_has_content = (current_data_.empty() == false);
        if (data_already_has_content) {
            current_data_ += '\n';
        }
        current_data_ += field_value;
    }
    else if (field_name == "retry") {
        // Parse retry value (milliseconds) - per SSE spec, must be digits only
        std::uint32_t retry_ms = 0;
        auto [ptr, ec] = std::from_chars(
            field_value.data(), 
            field_value.data() + field_value.size(), 
            retry_ms
        );
        // Only set if entire value was parsed successfully (no trailing chars)
        if (ec == std::errc{} && ptr == field_value.data() + field_value.size()) {
            current_retry_ = retry_ms;
        }
        // Invalid retry values are silently ignored per spec
    }

    return false;  // Event not complete yet
}

SseEvent SseParser::emit_event() {
    SseEvent event{
        std::move(current_id_),
        std::move(current_event_),
        std::move(current_data_),
        current_retry_
    };

    current_data_.clear();
    current_id_ = std::nullopt;
    current_event_ = std::nullopt;
    current_retry_ = std::nullopt;

    return event;
}

}  // namespace mcpp
