#include "mcpp/protocol/json_rpc.hpp"

#include <stdexcept>

namespace mcpp {
namespace {
constexpr std::string_view kJsonRpcVersion{"2.0"};

bool is_valid_params_type(const Json& node) {
    const bool is_object = node.is_object();
    const bool is_array = node.is_array();
    return (is_object == true) || (is_array == true);
}

JsonResult<JsonRpcId> parse_id_field(const Json& id_node) {
    if (id_node.is_number_integer() == true) {
        return JsonRpcId::integer(id_node.get<std::int64_t>());
    }
    if (id_node.is_string() == true) {
        return JsonRpcId::string(id_node.get<std::string>());
    }

    return tl::unexpected(JsonError{
        JsonError::Code::InvalidId,
        "id must be an integer or string"});
}
}  // namespace

JsonRpcId JsonRpcId::integer(std::int64_t value) {
    return JsonRpcId{value};
}

JsonRpcId JsonRpcId::string(std::string value) {
    return JsonRpcId{std::move(value)};
}

JsonRpcRequest::JsonRpcRequest(std::string method,
                               std::int64_t id,
                               std::optional<Json> params)
    : JsonRpcRequest(std::move(method), JsonRpcId::integer(id), std::move(params)) {}

JsonRpcRequest::JsonRpcRequest(std::string method,
                               std::string id,
                               std::optional<Json> params)
    : JsonRpcRequest(std::move(method), JsonRpcId::string(std::move(id)), std::move(params)) {}

JsonRpcRequest::JsonRpcRequest(std::string method,
                               JsonRpcId id,
                               std::optional<Json> params)
    : method_(std::move(method)),
      id_(std::move(id)),
      params_(std::move(params)) {}

const std::string& JsonRpcRequest::method() const noexcept {
    return method_;
}

const JsonRpcId& JsonRpcRequest::id() const noexcept {
    return id_;
}

const std::optional<Json>& JsonRpcRequest::params() const noexcept {
    return params_;
}

Json JsonRpcRequest::to_json() const {
    Json payload = Json::object();
    payload["jsonrpc"] = kJsonRpcVersion;
    payload["method"] = method_;
    std::visit(
        [&](const auto& id_value) {
            payload["id"] = id_value;
        },
        id_.value);

    if (params_.has_value()) {
        payload["params"] = *params_;
    }
    return payload;
}

JsonResult<JsonRpcRequest> JsonRpcRequest::from_json(const Json& payload) {
    if (payload.is_object() == false) {
        return tl::unexpected(JsonError{
            JsonError::Code::InvalidParams,
            "payload must be a JSON object"});
    }

    const bool has_version_field = payload.contains("jsonrpc");
    if (has_version_field == false) {
        return tl::unexpected(JsonError{
            JsonError::Code::MissingField,
            "missing jsonrpc version field"});
    }

    const Json& version_node = payload.at("jsonrpc");
    const bool version_is_string = version_node.is_string();
    if ((version_is_string == false) || (version_node != kJsonRpcVersion)) {
        return tl::unexpected(JsonError{
            JsonError::Code::InvalidVersion,
            "jsonrpc must equal \"2.0\""});
    }

    const bool has_method_field = payload.contains("method");
    if (has_method_field == false) {
        return tl::unexpected(JsonError{
            JsonError::Code::MissingField,
            "missing method field"});
    }

    const Json& method_node = payload.at("method");
    const bool method_is_string = method_node.is_string();
    if (method_is_string == false) {
        return tl::unexpected(JsonError{
            JsonError::Code::InvalidParams,
            "method must be a string"});
    }

    const bool has_id_field = payload.contains("id");
    if (has_id_field == false) {
        return tl::unexpected(JsonError{
            JsonError::Code::InvalidId,
            "missing id field"});
    }
    const Json& id_node = payload.at("id");
    auto parsed_id = parse_id_field(id_node);
    if (parsed_id.has_value() == false) {
        return tl::unexpected(parsed_id.error());
    }

    std::optional<Json> parsed_params;
    const bool has_params_field = payload.contains("params");
    if (has_params_field == true) {
        const Json& params_node = payload.at("params");
        const bool params_are_valid = is_valid_params_type(params_node);
        if (params_are_valid == false) {
            return tl::unexpected(JsonError{
                JsonError::Code::InvalidParams,
                "params must be an object or array"});
        }
        parsed_params = params_node;
    }

    return JsonRpcRequest(
        method_node.get<std::string>(),
        *parsed_id,
        parsed_params);
}

JsonRpcNotification::JsonRpcNotification(std::string method,
                                         std::optional<Json> params)
    : method_(std::move(method)),
      params_(std::move(params)) {}

const std::string& JsonRpcNotification::method() const noexcept {
    return method_;
}

const std::optional<Json>& JsonRpcNotification::params() const noexcept {
    return params_;
}

Json JsonRpcNotification::to_json() const {
    Json payload = Json::object();
    payload["jsonrpc"] = kJsonRpcVersion;
    payload["method"] = method_;
    if (params_.has_value()) {
        payload["params"] = *params_;
    }
    return payload;
}

Json JsonRpcError::to_json() const {
    Json payload;
    payload["code"] = code;
    payload["message"] = message;
    if (data.has_value()) {
        payload["data"] = *data;
    }
    return payload;
}

}  // namespace mcpp


