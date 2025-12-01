#include "mcpp/json/fast_json.hpp"

namespace mcpp {

// ─────────────────────────────────────────────────────────────────────────────
// FastJsonParser Implementation
// ─────────────────────────────────────────────────────────────────────────────

JsonResult FastJsonParser::parse(std::string_view json_str) {
    // simdjson requires padded input for safety
    simdjson::padded_string padded(json_str);
    return parse_padded(padded);
}

JsonResult FastJsonParser::parse_padded(simdjson::padded_string_view json_str) {
    // Parse with simdjson
    auto doc_result = parser_.iterate(json_str);
    const bool parse_failed = doc_result.error() != simdjson::SUCCESS;
    if (parse_failed) {
        return tl::unexpected(JsonParseError(
            std::string(simdjson::error_message(doc_result.error())),
            0
        ));
    }

    // Convert to nlohmann::json with depth tracking
    try {
        auto doc = std::move(doc_result).value();
        auto value = doc.get_value();
        const bool get_failed = value.error() != simdjson::SUCCESS;
        if (get_failed) {
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(value.error())),
                0
            ));
        }
        return convert(value.value(), 0);
    } catch (const simdjson::simdjson_error& e) {
        return tl::unexpected(JsonParseError(e.what(), 0));
    }
}

JsonResult FastJsonParser::convert(simdjson::ondemand::value value, std::size_t depth) {
    // Check depth limit to prevent stack overflow
    if (depth > config_.max_depth) {
        return tl::unexpected(JsonParseError(
            "Maximum nesting depth exceeded (" + std::to_string(config_.max_depth) + ")",
            0
        ));
    }

    // Get the type of the value
    auto type_result = value.type();
    const bool type_failed = type_result.error() != simdjson::SUCCESS;
    if (type_failed) {
        return tl::unexpected(JsonParseError(
            std::string(simdjson::error_message(type_result.error())),
            0
        ));
    }

    switch (type_result.value()) {
        case simdjson::ondemand::json_type::object: {
            auto obj = value.get_object();
            const bool obj_ok = (obj.error() == simdjson::SUCCESS);
            if (obj_ok) {
                return convert_object(obj.value(), depth + 1);
            }
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(obj.error())),
                0
            ));
        }

        case simdjson::ondemand::json_type::array: {
            auto arr = value.get_array();
            const bool arr_ok = (arr.error() == simdjson::SUCCESS);
            if (arr_ok) {
                return convert_array(arr.value(), depth + 1);
            }
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(arr.error())),
                0
            ));
        }

        case simdjson::ondemand::json_type::string: {
            auto str = value.get_string();
            const bool str_ok = (str.error() == simdjson::SUCCESS);
            if (str_ok) {
                // Direct construction from string_view avoids intermediate copy
                return nlohmann::json(std::string(str.value()));
            }
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(str.error())),
                0
            ));
        }

        case simdjson::ondemand::json_type::number: {
            // Try integer first, then double
            auto int_val = value.get_int64();
            const bool is_int = (int_val.error() == simdjson::SUCCESS);
            if (is_int) {
                return nlohmann::json(int_val.value());
            }

            auto uint_val = value.get_uint64();
            const bool is_uint = (uint_val.error() == simdjson::SUCCESS);
            if (is_uint) {
                return nlohmann::json(uint_val.value());
            }

            auto double_val = value.get_double();
            const bool is_double = (double_val.error() == simdjson::SUCCESS);
            if (is_double) {
                return nlohmann::json(double_val.value());
            }

            return tl::unexpected(JsonParseError("Failed to parse number", 0));
        }

        case simdjson::ondemand::json_type::boolean: {
            auto bool_val = value.get_bool();
            const bool bool_ok = (bool_val.error() == simdjson::SUCCESS);
            if (bool_ok) {
                return nlohmann::json(bool_val.value());
            }
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(bool_val.error())),
                0
            ));
        }

        case simdjson::ondemand::json_type::null:
            return nlohmann::json(nullptr);
    }

    return tl::unexpected(JsonParseError("Unknown JSON type", 0));
}

JsonResult FastJsonParser::convert_object(simdjson::ondemand::object obj, std::size_t depth) {
    nlohmann::json result = nlohmann::json::object();

    for (auto field : obj) {
        auto key_result = field.unescaped_key();
        const bool key_ok = (key_result.error() == simdjson::SUCCESS);
        if (key_ok == false) {
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(key_result.error())),
                0
            ));
        }

        auto val_result = field.value();
        const bool val_ok = (val_result.error() == simdjson::SUCCESS);
        if (val_ok == false) {
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(val_result.error())),
                0
            ));
        }

        // Convert key directly from string_view
        std::string_view key_sv = key_result.value();
        auto converted = convert(val_result.value(), depth);
        if (!converted) {
            return converted;  // Propagate error
        }
        result[std::string(key_sv)] = std::move(*converted);
    }

    return result;
}

JsonResult FastJsonParser::convert_array(simdjson::ondemand::array arr, std::size_t depth) {
    nlohmann::json result = nlohmann::json::array();

    for (auto element : arr) {
        const bool elem_ok = (element.error() == simdjson::SUCCESS);
        if (!elem_ok) {
            return tl::unexpected(JsonParseError(
                std::string(simdjson::error_message(element.error())),
                0
            ));
        }
        
        auto converted = convert(element.value(), depth);
        if (!converted) {
            return converted;  // Propagate error
        }
        result.push_back(std::move(*converted));
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Convenience Functions
// ─────────────────────────────────────────────────────────────────────────────

JsonResult fast_parse(std::string_view json_str) {
    // Thread-local parser for convenience
    thread_local FastJsonParser parser;
    return parser.parse(json_str);
}

std::string fast_json_implementation() {
    // Returns which SIMD implementation is being used
    const simdjson::implementation* impl = simdjson::get_active_implementation();
    return std::string(impl->name());
}

}  // namespace mcpp

