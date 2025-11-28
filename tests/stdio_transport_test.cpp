#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <sstream>
#include <future>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "mcpp/transport/stdio_transport.hpp"

using json = nlohmann::json;
using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────────────────────
// MockPipeStream - Simulates a pipe with blocking reads and controlled writes
// ─────────────────────────────────────────────────────────────────────────────
// This class provides a thread-safe stream that:
// - Blocks on read until data is available (like a real pipe)
// - Allows another thread to write data
// - Can be closed to unblock waiting readers

class MockPipeStream : public std::iostream {
public:
    MockPipeStream() : std::iostream(&buffer_) {}

    // Write data to the pipe (thread-safe, unblocks readers)
    void write_data(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_data_ += data;
        cv_.notify_all();
    }

    // Close the pipe (unblocks readers with EOF)
    void close_pipe() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    // Check if closed
    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    class PipeStreamBuf : public std::streambuf {
    public:
        PipeStreamBuf(MockPipeStream& parent) : parent_(parent) {}

    protected:
        int underflow() override {
            std::unique_lock<std::mutex> lock(parent_.mutex_);
            
            // Wait for data or close
            parent_.cv_.wait(lock, [this] {
                return !parent_.pending_data_.empty() || parent_.closed_;
            });

            if (parent_.pending_data_.empty() && parent_.closed_) {
                return traits_type::eof();
            }

            // Move pending data to read buffer
            read_buffer_ = std::move(parent_.pending_data_);
            parent_.pending_data_.clear();
            
            setg(read_buffer_.data(), 
                 read_buffer_.data(), 
                 read_buffer_.data() + read_buffer_.size());
            
            return traits_type::to_int_type(*gptr());
        }

    private:
        MockPipeStream& parent_;
        std::string read_buffer_;
    };

    PipeStreamBuf buffer_{*this};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::string pending_data_;
    bool closed_ = false;
};

TEST_CASE("StdioTransport::send writes RFC-style frames", "[transport][stdio]") {
    std::stringstream source;  // dummy input (not used)
    std::stringstream sink;
    mcpp::StdioTransportConfig config;
    config.input = &source;
    config.output = &sink;

    mcpp::StdioTransport transport{config};
    // NOTE: We do NOT call start() so no reader thread runs; sync-only usage.

    json payload = {
        {"jsonrpc", "2.0"},
        {"method", "tools/list"},
        {"id", 7}
    };

    auto send_result = transport.send(payload);
    REQUIRE(send_result.has_value());

    const std::string written = sink.str();
    const auto header_end = written.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);

    const std::string header = written.substr(0, header_end);
    const std::string body = written.substr(header_end + 4);

    REQUIRE(body == payload.dump());
    REQUIRE(header == "Content-Length: " + std::to_string(body.size()));
}

TEST_CASE("StdioTransport::receive parses framed JSON payloads", "[transport][stdio]") {
    const std::string first_body = R"({"jsonrpc":"2.0","method":"ping","id":1})";
    const std::string second_body = R"({"jsonrpc":"2.0","method":"pong"})";

    std::string framed_input;
    framed_input += "Content-Length: " + std::to_string(first_body.size()) + "\r\n\r\n" + first_body;
    framed_input += "Content-Length: " + std::to_string(second_body.size()) + "\r\n\r\n" + second_body;

    std::stringstream source{framed_input};
    std::stringstream sink;

    mcpp::StdioTransportConfig config;
    config.input = &source;
    config.output = &sink;

    mcpp::StdioTransport transport{config};
    // NOTE: We do NOT call start() so no reader thread runs; sync-only usage.

    auto first = transport.receive();
    REQUIRE(first.has_value());
    REQUIRE(first->at("method") == "ping");
    REQUIRE(first->at("id") == 1);

    auto second = transport.receive();
    REQUIRE(second.has_value());
    REQUIRE(second->at("method") == "pong");
    REQUIRE_FALSE(second->contains("id"));
}

TEST_CASE("StdioTransport::receive surfaces malformed frames", "[transport][stdio]") {
    std::string malformed = "Content-Length: 99\r\n\r\n{\"jsonrpc\":\"2.0\"}";
    std::stringstream source{malformed};
    std::stringstream sink;  // dummy output (not used)

    mcpp::StdioTransportConfig config;
    config.input = &source;
    config.output = &sink;

    mcpp::StdioTransport transport{config};
    // NOTE: We do NOT call start() so no reader thread runs; sync-only usage.

    auto result = transport.receive();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().category == mcpp::TransportError::Category::Protocol);
}

TEST_CASE("StdioTransport async APIs bridge send and receive", "[transport][stdio][async]") {
    MockPipeStream mock_input;
    std::stringstream sink;

    mcpp::StdioTransportConfig config;
    config.input = &mock_input;
    config.output = &sink;

    mcpp::StdioTransport transport{config};
    transport.start();  // Enable async mode: spawns io_thread_ and reader_thread_

    const json outbound = {
        {"jsonrpc", "2.0"},
        {"method", "ping"},
        {"id", 99}
    };

    // Write inbound message to mock pipe (simulates server response)
    const std::string inbound_body = R"({"jsonrpc":"2.0","method":"pong"})";
    std::string framed_input;
    framed_input += "Content-Length: " + std::to_string(inbound_body.size()) + "\r\n\r\n" + inbound_body;
    
    // Start a thread to write data after a small delay
    std::thread writer([&mock_input, &framed_input]() {
        std::this_thread::sleep_for(50ms);
        mock_input.write_data(framed_input);
    });

    auto fut = asio::co_spawn(
        transport.executor(),
        [&transport, outbound]() -> asio::awaitable<void> {
            auto send_result = co_await transport.async_send(outbound);
            REQUIRE(send_result.has_value());

            auto recv_result = co_await transport.async_receive();
            REQUIRE(recv_result.has_value());
            REQUIRE(recv_result->at("method") == "pong");
            co_return;
        },
        asio::use_future);

    REQUIRE_NOTHROW(fut.get());
    REQUIRE(sink.str().find(outbound.dump()) != std::string::npos);

    writer.join();
    mock_input.close_pipe();  // Unblock reader thread
    transport.stop();
}

TEST_CASE("StdioTransport async handles multiple messages", "[transport][stdio][async]") {
    MockPipeStream mock_input;
    std::stringstream sink;

    mcpp::StdioTransportConfig config;
    config.input = &mock_input;
    config.output = &sink;

    mcpp::StdioTransport transport{config};
    transport.start();

    // Write two messages
    std::thread writer([&mock_input]() {
        std::this_thread::sleep_for(50ms);
        
        const std::string msg1 = R"({"jsonrpc":"2.0","method":"first","id":1})";
        std::string frame1 = "Content-Length: " + std::to_string(msg1.size()) + "\r\n\r\n" + msg1;
        mock_input.write_data(frame1);

        std::this_thread::sleep_for(50ms);

        const std::string msg2 = R"({"jsonrpc":"2.0","method":"second","id":2})";
        std::string frame2 = "Content-Length: " + std::to_string(msg2.size()) + "\r\n\r\n" + msg2;
        mock_input.write_data(frame2);
    });

    auto fut = asio::co_spawn(
        transport.executor(),
        [&transport]() -> asio::awaitable<void> {
            auto result1 = co_await transport.async_receive();
            REQUIRE(result1.has_value());
            REQUIRE(result1->at("method") == "first");

            auto result2 = co_await transport.async_receive();
            REQUIRE(result2.has_value());
            REQUIRE(result2->at("method") == "second");
            co_return;
        },
        asio::use_future);

    REQUIRE_NOTHROW(fut.get());

    writer.join();
    mock_input.close_pipe();
    transport.stop();
}

TEST_CASE("StdioTransport async stops cleanly on pipe close", "[transport][stdio][async]") {
    MockPipeStream mock_input;
    std::stringstream sink;

    mcpp::StdioTransportConfig config;
    config.input = &mock_input;
    config.output = &sink;

    mcpp::StdioTransport transport{config};
    transport.start();

    // Close pipe immediately - should cause reader to exit
    std::thread closer([&mock_input]() {
        std::this_thread::sleep_for(100ms);
        mock_input.close_pipe();
    });

    // Give reader thread time to start and block
    std::this_thread::sleep_for(50ms);

    closer.join();
    
    // stop() should not hang because pipe is closed
    transport.stop();
    
    // If we get here, the test passed (no hang)
    REQUIRE(true);
}

