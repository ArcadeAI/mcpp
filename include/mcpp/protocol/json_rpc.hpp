#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <tl/expected.hpp>

namespace mcpp {

using Json = nlohmann::json;

struct JsonError {
    enum class Code {
        InvalidVersion,
        MissingField,
        InvalidId,
        InvalidParams,
        Internal
    };

    Code code{Code::Internal};
    std::string message;
};

template <typename T>
using JsonResult = tl::expected<T, JsonError>;

struct JsonRpcId {
    std::variant<std::int64_t, std::string> value;

    static JsonRpcId integer(std::int64_t v);
    static JsonRpcId string(std::string v);
};

class JsonRpcRequest {
public:
    JsonRpcRequest(std::string method, std::int64_t id, std::optional<Json> params = std::nullopt);
    JsonRpcRequest(std::string method, std::string id, std::optional<Json> params = std::nullopt);
    JsonRpcRequest(std::string method, JsonRpcId id, std::optional<Json> params = std::nullopt);

    [[nodiscard]] const std::string& method() const noexcept;
    [[nodiscard]] const JsonRpcId& id() const noexcept;
    [[nodiscard]] const std::optional<Json>& params() const noexcept;

    [[nodiscard]] Json to_json() const;
    static JsonResult<JsonRpcRequest> from_json(const Json& payload);

private:
    std::string method_;
    JsonRpcId id_;
    std::optional<Json> params_;
};

class JsonRpcNotification {
public:
    explicit JsonRpcNotification(std::string method, std::optional<Json> params = std::nullopt);

    [[nodiscard]] const std::string& method() const noexcept;
    [[nodiscard]] const std::optional<Json>& params() const noexcept;

    [[nodiscard]] Json to_json() const;

private:
    std::string method_;
    std::optional<Json> params_;
};

// Placeholder for future response/error types; giving clues for follow-up work.
struct JsonRpcError {
    std::int64_t code{};
    std::string message;
    std::optional<Json> data{};

    [[nodiscard]] Json to_json() const;
};

}  // namespace mcpp


