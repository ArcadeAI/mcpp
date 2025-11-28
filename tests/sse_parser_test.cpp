#include <catch2/catch_test_macros.hpp>
#include "mcpp/transport/sse_parser.hpp"

using mcpp::SseParser;
using mcpp::SseEvent;

TEST_CASE("SseParser parses single event", "[sse][parser]") {
    SseParser parser;

    // A complete event: data line followed by blank line
    auto events = parser.feed("data: hello world\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello world");
    REQUIRE(events[0].id.has_value() == false);
    REQUIRE(events[0].event.has_value() == false);
}

TEST_CASE("SseParser parses event with all fields", "[sse][parser]") {
    SseParser parser;

    auto events = parser.feed("event: message\nid: 42\ndata: {\"test\":true}\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event.value() == "message");
    REQUIRE(events[0].id.value() == "42");
    REQUIRE(events[0].data == "{\"test\":true}");
}

TEST_CASE("SseParser concatenates multiple data lines", "[sse][parser]") {
    SseParser parser;

    // Multiple data: lines should be joined with newlines
    auto events = parser.feed("data: line one\ndata: line two\ndata: line three\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "line one\nline two\nline three");
}

TEST_CASE("SseParser handles chunked input", "[sse][parser]") {
    SseParser parser;

    // Data arrives in arbitrary chunks
    auto events1 = parser.feed("data: hel");
    REQUIRE(events1.empty());  // No complete event yet

    auto events2 = parser.feed("lo wor");
    REQUIRE(events2.empty());  // Still incomplete

    auto events3 = parser.feed("ld\n\n");
    REQUIRE(events3.size() == 1);
    REQUIRE(events3[0].data == "hello world");
}

TEST_CASE("SseParser ignores comment lines", "[sse][parser]") {
    SseParser parser;

    // Lines starting with : are comments
    auto events = parser.feed(": this is a comment\ndata: actual data\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "actual data");
}

TEST_CASE("SseParser handles empty data field", "[sse][parser]") {
    SseParser parser;

    // "data:" with nothing after it
    auto events = parser.feed("data:\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "");
}

TEST_CASE("SseParser handles data field with no space after colon", "[sse][parser]") {
    SseParser parser;

    // Per SSE spec: space after colon is optional
    auto events = parser.feed("data:no-space\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "no-space");
}

TEST_CASE("SseParser parses multiple events in one chunk", "[sse][parser]") {
    SseParser parser;

    auto events = parser.feed("data: first\n\ndata: second\n\n");

    REQUIRE(events.size() == 2);
    REQUIRE(events[0].data == "first");
    REQUIRE(events[1].data == "second");
}

TEST_CASE("SseParser handles CRLF line endings", "[sse][parser]") {
    SseParser parser;

    // Windows-style line endings
    auto events = parser.feed("data: hello\r\n\r\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "hello");
}

TEST_CASE("SseParser ignores unknown fields", "[sse][parser]") {
    SseParser parser;

    // Unknown field names should be ignored
    auto events = parser.feed("unknown: value\ndata: actual\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "actual");
}

TEST_CASE("SseParser reset clears state", "[sse][parser]") {
    SseParser parser;

    // Start an event but don't finish it
    auto events1 = parser.feed("data: partial");
    REQUIRE(events1.empty());

    // Reset discards buffered data
    parser.reset();

    // New event should work independently
    auto events2 = parser.feed("data: fresh start\n\n");
    REQUIRE(events2.size() == 1);
    REQUIRE(events2[0].data == "fresh start");
}

TEST_CASE("SseParser parses retry field", "[sse][parser]") {
    SseParser parser;

    auto events = parser.feed("retry: 3000\ndata: with retry hint\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "with retry hint");
    REQUIRE(events[0].retry.has_value());
    REQUIRE(events[0].retry.value() == 3000);
}

TEST_CASE("SseParser ignores invalid retry values", "[sse][parser]") {
    SseParser parser;

    // Non-numeric retry should be ignored
    auto events = parser.feed("retry: abc\ndata: test\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "test");
    REQUIRE(events[0].retry.has_value() == false);
}

TEST_CASE("SseParser ignores retry with trailing characters", "[sse][parser]") {
    SseParser parser;

    // Per SSE spec, retry must be digits only
    auto events = parser.feed("retry: 3000ms\ndata: test\n\n");

    REQUIRE(events.size() == 1);
    REQUIRE(events[0].retry.has_value() == false);  // "3000ms" is not valid
}

TEST_CASE("SseParser retry field does not persist across events", "[sse][parser]") {
    SseParser parser;

    auto events = parser.feed("retry: 5000\ndata: first\n\ndata: second\n\n");

    REQUIRE(events.size() == 2);
    REQUIRE(events[0].retry.has_value());
    REQUIRE(events[0].retry.value() == 5000);
    REQUIRE(events[1].retry.has_value() == false);  // Retry doesn't carry over
}

