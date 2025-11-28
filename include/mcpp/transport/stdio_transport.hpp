#pragma once

#include <atomic>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/experimental/channel.hpp>

#include "mcpp/transport.hpp"

namespace mcpp {

struct StdioTransportConfig {
    std::istream* input{nullptr};
    std::ostream* output{nullptr};
    bool auto_flush{true};
    std::size_t max_content_length{1 << 20};  // 1 MiB default
};

class StdioTransport {
public:
    explicit StdioTransport(StdioTransportConfig config);
    ~StdioTransport();

    [[nodiscard]] TransportResult<void> send(const Json& message);
    [[nodiscard]] TransportResult<Json> receive();

    asio::awaitable<TransportResult<void>> async_send(Json message);
    asio::awaitable<TransportResult<Json>> async_receive();
    void start();
    void stop();
    [[nodiscard]] asio::any_io_executor executor() noexcept;
    [[nodiscard]] const StdioTransportConfig& config() const noexcept;

private:
    void reader_loop();

    StdioTransportConfig config_;
    asio::io_context io_;
    asio::strand<asio::io_context::executor_type> strand_;
    asio::experimental::channel<void(asio::error_code, TransportResult<Json>)> channel_;
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
    std::thread io_thread_;
    std::thread reader_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace mcpp


