#ifndef MCPP_TESTS_MOCK_MCP_SERVER_HPP
#define MCPP_TESTS_MOCK_MCP_SERVER_HPP

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mcpp::testing {

using Json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// MockMcpServer
// ─────────────────────────────────────────────────────────────────────────────
// A mock MCP server for integration testing. Simulates server behavior:
// - Handles initialize/initialized handshake
// - Responds to tools/list, resources/list, etc.
// - Generates session IDs
// - Supports SSE-style notifications
//
// Usage:
//   MockMcpServer server;
//   server.on_request("tools/list", [](const Json& params) {
//       return Json{{"tools", Json::array()}};
//   });
//   server.start();
//   
//   // Connect HttpTransport to server.endpoint()
//   // ... run tests ...
//   
//   server.stop();

class MockMcpServer {
public:
    using RequestHandler = std::function<Json(const Json& params)>;
    using NotificationHandler = std::function<void(const Json& params)>;

    struct ServerCapabilities {
        bool tools = true;
        bool resources = true;
        bool prompts = true;
        bool logging = false;
    };

    struct ServerInfo {
        std::string name = "MockMcpServer";
        std::string version = "1.0.0";
    };

    MockMcpServer() = default;
    ~MockMcpServer() = default;

    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────

    void set_capabilities(ServerCapabilities caps) { capabilities_ = caps; }
    void set_server_info(ServerInfo info) { server_info_ = info; }

    // Register handler for a specific method
    void on_request(const std::string& method, RequestHandler handler) {
        request_handlers_[method] = std::move(handler);
    }

    void on_notification(const std::string& method, NotificationHandler handler) {
        notification_handlers_[method] = std::move(handler);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Message Processing (for direct testing without HTTP)
    // ─────────────────────────────────────────────────────────────────────────

    /// Process a JSON-RPC request and return the response
    Json process_request(const Json& request) {
        const auto method = request.value("method", "");
        const auto id = request.value("id", Json{});
        const auto params = request.value("params", Json::object());

        // Handle built-in methods
        if (method == "initialize") {
            return make_response(id, handle_initialize(params));
        }

        if (method == "notifications/initialized") {
            initialized_ = true;
            return Json{};  // No response for notifications
        }

        // Check for registered handler
        auto it = request_handlers_.find(method);
        if (it != request_handlers_.end()) {
            try {
                auto result = it->second(params);
                return make_response(id, result);
            } catch (const std::exception& e) {
                return make_error(id, -32000, e.what());
            }
        }

        // Method not found
        return make_error(id, -32601, "Method not found: " + method);
    }

    /// Process a notification (no response expected)
    void process_notification(const Json& notification) {
        const auto method = notification.value("method", "");
        const auto params = notification.value("params", Json::object());

        auto it = notification_handlers_.find(method);
        if (it != notification_handlers_.end()) {
            it->second(params);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Server-Initiated Messages
    // ─────────────────────────────────────────────────────────────────────────

    /// Queue a notification to be sent to the client
    void send_notification(const std::string& method, const Json& params = {}) {
        std::lock_guard<std::mutex> lock(outbound_mutex_);
        Json notification = {
            {"jsonrpc", "2.0"},
            {"method", method}
        };
        if (!params.empty()) {
            notification["params"] = params;
        }
        outbound_queue_.push(notification);
        outbound_cv_.notify_all();
    }

    /// Get next outbound message (blocks until available or timeout)
    std::optional<Json> get_outbound_message(std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
        std::unique_lock<std::mutex> lock(outbound_mutex_);
        if (outbound_cv_.wait_for(lock, timeout, [this] { return !outbound_queue_.empty(); })) {
            auto msg = outbound_queue_.front();
            outbound_queue_.pop();
            return msg;
        }
        return std::nullopt;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Session Management
    // ─────────────────────────────────────────────────────────────────────────

    std::string session_id() const { return session_id_; }
    bool is_initialized() const { return initialized_; }

    void expire_session() {
        session_id_.clear();
        initialized_ = false;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Request History (for assertions)
    // ─────────────────────────────────────────────────────────────────────────

    std::vector<Json> received_requests() const {
        std::lock_guard<std::mutex> lock(history_mutex_);
        return received_requests_;
    }

    void clear_history() {
        std::lock_guard<std::mutex> lock(history_mutex_);
        received_requests_.clear();
    }

    std::size_t request_count() const {
        std::lock_guard<std::mutex> lock(history_mutex_);
        return received_requests_.size();
    }

private:
    Json handle_initialize(const Json& params) {
        // Generate session ID
        session_id_ = "mock-session-" + std::to_string(++session_counter_);

        // Build capabilities response
        Json caps = Json::object();
        if (capabilities_.tools) {
            caps["tools"] = Json::object();
        }
        if (capabilities_.resources) {
            caps["resources"] = Json::object();
        }
        if (capabilities_.prompts) {
            caps["prompts"] = Json::object();
        }
        if (capabilities_.logging) {
            caps["logging"] = Json::object();
        }

        return {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", caps},
            {"serverInfo", {
                {"name", server_info_.name},
                {"version", server_info_.version}
            }}
        };
    }

    Json make_response(const Json& id, const Json& result) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result}
        };
    }

    Json make_error(const Json& id, int code, const std::string& message) {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {
                {"code", code},
                {"message", message}
            }}
        };
    }

    void record_request(const Json& request) {
        std::lock_guard<std::mutex> lock(history_mutex_);
        received_requests_.push_back(request);
    }

    // Configuration
    ServerCapabilities capabilities_;
    ServerInfo server_info_;

    // Handlers
    std::unordered_map<std::string, RequestHandler> request_handlers_;
    std::unordered_map<std::string, NotificationHandler> notification_handlers_;

    // State
    std::string session_id_;
    bool initialized_ = false;
    static inline std::atomic<int> session_counter_{0};

    // Outbound messages
    std::queue<Json> outbound_queue_;
    std::mutex outbound_mutex_;
    std::condition_variable outbound_cv_;

    // History
    mutable std::mutex history_mutex_;
    std::vector<Json> received_requests_;
};

// ─────────────────────────────────────────────────────────────────────────────
// MockMcpHttpClient - Bridges MockMcpServer with IHttpClient interface
// ─────────────────────────────────────────────────────────────────────────────
// Allows HttpTransport to talk to MockMcpServer without real HTTP.

class MockMcpHttpClient : public IHttpClient {
public:
    explicit MockMcpHttpClient(MockMcpServer& server) : server_(server) {}

    void set_base_url(const std::string& url) override { base_url_ = url; }
    void set_default_headers(const HeaderMap& headers) override { default_headers_ = headers; }
    void set_connect_timeout(std::chrono::milliseconds timeout) override { connect_timeout_ = timeout; }
    void set_read_timeout(std::chrono::milliseconds timeout) override { read_timeout_ = timeout; }
    void set_verify_ssl(bool verify) override { verify_ssl_ = verify; }

    HttpClientResult<HttpClientResponse> get(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        // SSE endpoint - return pending notifications
        auto msg = server_.get_outbound_message(std::chrono::milliseconds{100});
        if (msg) {
            std::string event_data = "event: message\ndata: " + msg->dump() + "\n\n";
            return HttpClientResponse{
                200,
                {{"Content-Type", "text/event-stream"}},
                event_data
            };
        }
        return HttpClientResponse{200, {{"Content-Type", "text/event-stream"}}, ""};
    }

    HttpClientResult<HttpClientResponse> post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers = {}
    ) override {
        try {
            auto request = Json::parse(body);
            auto response = server_.process_request(request);

            // Check if it's a notification (no response)
            if (response.empty()) {
                return HttpClientResponse{202, {}, ""};
            }

            HeaderMap response_headers;
            response_headers["Content-Type"] = "application/json";
            
            // Add session ID on initialize response
            if (request.value("method", "") == "initialize") {
                response_headers["Mcp-Session-Id"] = server_.session_id();
            }

            return HttpClientResponse{
                200,
                response_headers,
                response.dump()
            };
        } catch (const std::exception& e) {
            return tl::unexpected(HttpClientError::unknown(e.what()));
        }
    }

    HttpClientResult<HttpClientResponse> del(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        server_.expire_session();
        return HttpClientResponse{200, {}, ""};
    }

    std::future<HttpClientResult<HttpClientResponse>> async_get(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        return std::async(std::launch::async, [this, path, headers]() {
            return get(path, headers);
        });
    }

    std::future<HttpClientResult<HttpClientResponse>> async_post(
        const std::string& path,
        const std::string& body,
        const std::string& content_type,
        const HeaderMap& headers = {}
    ) override {
        return std::async(std::launch::async, [this, path, body, content_type, headers]() {
            return post(path, body, content_type, headers);
        });
    }

    std::future<HttpClientResult<HttpClientResponse>> async_del(
        const std::string& path,
        const HeaderMap& headers = {}
    ) override {
        return std::async(std::launch::async, [this, path, headers]() {
            return del(path, headers);
        });
    }

    void cancel() override {}

private:
    MockMcpServer& server_;
    std::string base_url_;
    HeaderMap default_headers_;
    std::chrono::milliseconds connect_timeout_{10000};
    std::chrono::milliseconds read_timeout_{30000};
    bool verify_ssl_ = true;
};

}  // namespace mcpp::testing

#endif  // MCPP_TESTS_MOCK_MCP_SERVER_HPP

