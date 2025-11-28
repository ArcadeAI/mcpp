#include "mcpp/transport/stdio_transport.hpp"

#include <asio/use_awaitable.hpp>
#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace mcpp {
namespace {

TransportResult<Json> protocol_error(std::string message) {
    return tl::unexpected(TransportError{
        TransportError::Category::Protocol,
        std::move(message)});
}

TransportResult<Json> stream_closed_error() {
    return tl::unexpected(TransportError{
        TransportError::Category::Network,
        "end of stream"});
}

}  // namespace

StdioTransport::StdioTransport(StdioTransportConfig config)
    : config_(std::move(config)),
      strand_(asio::make_strand(io_)),
      channel_(io_, 16) {  // Buffer up to 16 messages

    work_guard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(io_.get_executor());
}

StdioTransport::~StdioTransport() {
    stop();
}

const StdioTransportConfig& StdioTransport::config() const noexcept {
    return config_;
}

asio::any_io_executor StdioTransport::executor() noexcept {
    return io_.get_executor();
}

TransportResult<void> StdioTransport::send(const Json& message) {
    if (config_.output == nullptr) {
        return tl::unexpected(TransportError{
            TransportError::Category::Protocol,
            "output stream is not set"});
    }

    const std::string body = message.dump();
    const std::size_t body_size = body.size();

    if (body_size > config_.max_content_length) {
        return tl::unexpected(TransportError{
            TransportError::Category::Protocol,
            "body exceeds max_content_length"});
    }

    *config_.output << "Content-Length: " << body_size << "\r\n\r\n" << body;

    if (config_.auto_flush == true) {
        config_.output->flush();
    }

    if (config_.output->fail()) {
        return tl::unexpected(TransportError{
            TransportError::Category::Network,
            "failed to write to stdout"});
    }

    return TransportResult<void>{};
}

TransportResult<Json> StdioTransport::receive() {
    if (config_.input == nullptr) {
        return protocol_error("input stream is not open");
    }

    std::string header_line;
    std::size_t content_length = 0;
    bool saw_header = false;

    while (std::getline(*config_.input, header_line)) {
        if ((header_line.empty() == false) && (header_line.back() == '\r')) {
            header_line.pop_back();
        }
        if (header_line.empty()) {
            break;
        }
        if (header_line.rfind("Content-Length:", 0) == 0) {
            // Skip "Content-Length:" and any optional whitespace
            auto value_start = std::strlen("Content-Length:");
            while (value_start < header_line.size() && 
                   (header_line[value_start] == ' ' || header_line[value_start] == '\t')) {
                ++value_start;
            }
            const auto value = header_line.substr(value_start);
            try {
                content_length = static_cast<std::size_t>(std::stoull(value));
            } catch (const std::exception&) {
                return protocol_error("invalid Content-Length value");
            }
            saw_header = true;
        }
    }

    if ((saw_header == false) && config_.input->eof()) {
        return stream_closed_error();
    }

    if (saw_header == false) {
        return protocol_error("missing Content-Length");
    }

    if (content_length > config_.max_content_length) {
        return protocol_error("frame too large");
    }

    std::string body(content_length, '\0');
    config_.input->read(body.data(), static_cast<std::streamsize>(content_length));
    if (config_.input->gcount() != static_cast<std::streamsize>(content_length)) {
        return protocol_error("unexpected EOF while reading body");
    }

    try {
        return Json::parse(body);
    } catch (const Json::parse_error& err) {
        return protocol_error(std::string("invalid JSON: ") + err.what());
    }
}

asio::awaitable<TransportResult<void>> StdioTransport::async_send(Json message) {
    co_await asio::post(strand_, asio::use_awaitable);
    co_return send(message);
}

asio::awaitable<TransportResult<Json>> StdioTransport::async_receive() {
    auto result = co_await channel_.async_receive(asio::use_awaitable);
    co_return result;
}

void StdioTransport::start() {
    if (running_.exchange(true)) {
        return;
    }
    const bool input_is_null = (config_.input == nullptr);
    const bool output_is_null = (config_.output == nullptr);
    if ((input_is_null == true) || (output_is_null == true)) {
        running_ = false;
        throw std::invalid_argument("StdioTransport::start() requires non-null input and output streams");
    }
    io_.restart();
    io_thread_ = std::thread([this]() { io_.run(); });
    reader_thread_ = std::thread([this]() { reader_loop(); });
}

void StdioTransport::stop() {
    if (running_.exchange(false) == false) {
        return;
    }

    channel_.close();
    if (work_guard_) {
        work_guard_->reset();
        work_guard_.reset();
    }
    io_.stop();

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

void StdioTransport::reader_loop() {
    while (running_) {
        auto result = receive();
        const bool is_network_error =
            (result.has_value() == false) &&
            (result.error().category == TransportError::Category::Network);
        if (is_network_error) {
            break;
        }
        if (channel_.try_send(asio::error_code{}, result) == false) {
            break;
        }
    }
    channel_.close();
}

}  // namespace mcpp


