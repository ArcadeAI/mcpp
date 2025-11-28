#include "mcpp/transport/http_transport_config.hpp"

namespace mcpp {


HttpTransportConfig& HttpTransportConfig::with_bearer_token(const std::string& token) {
    // Bearer tokens go in the Authorization header.
    // Format: "Bearer <token>" (note the space after "Bearer").
    const std::string header_value = "Bearer " + token;
    default_headers["Authorization"] = header_value;
    return *this;
}

HttpTransportConfig& HttpTransportConfig::with_header(
    const std::string& name,
    const std::string& value
) {
    default_headers[name] = value;
    return *this;
}

HttpTransportConfig& HttpTransportConfig::with_connect_timeout(std::chrono::milliseconds timeout) {
    connect_timeout = timeout;
    return *this;
}

HttpTransportConfig& HttpTransportConfig::with_read_timeout(std::chrono::milliseconds timeout) {
    read_timeout = timeout;
    return *this;
}

HttpTransportConfig& HttpTransportConfig::with_max_retries(std::size_t retries) {
    max_retries = retries;
    return *this;
}

HttpTransportConfig& HttpTransportConfig::with_sse_reconnect_delay(std::chrono::milliseconds delay) {
    sse_reconnect_delay = delay;
    return *this;
}

}  // namespace mcpp

