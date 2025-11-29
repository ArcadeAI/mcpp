cmake_minimum_required(VERSION 3.20)

# Project declaration
project(mcpp VERSION 0.1.0 LANGUAGES CXX)

# C++ Standard: 20 (Required for Concepts and Coroutines)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Ensure strict adherence to the standard

# Generate compile_commands.json for clangd and other language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Lint/Static Analysis switches
option(MCPP_ENABLE_CLANG_TIDY "Run clang-tidy via CMake during builds" OFF)
option(MCPP_ENABLE_CPPCHECK "Run cppcheck via CMake during builds" OFF)
option(MCPP_ENABLE_INCLUDE_WHAT_YOU_USE "Run include-what-you-use via CMake during builds" OFF)

if(MCPP_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
    if(CLANG_TIDY_EXECUTABLE)
        set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXECUTABLE} "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
    else()
        message(WARNING "clang-tidy requested but not found")
    endif()
endif()

if(MCPP_ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
    if(CPPCHECK_EXECUTABLE)
        set(CMAKE_CXX_CPPCHECK
            ${CPPCHECK_EXECUTABLE}
            --std=c++20
            --enable=warning,performance,portability,style
            --inline-suppr
            --suppress=missingIncludeSystem
        )
    else()
        message(WARNING "cppcheck requested but not found")
    endif()
endif()

if(MCPP_ENABLE_INCLUDE_WHAT_YOU_USE)
    find_program(IWYU_EXECUTABLE NAMES include-what-you-use iwyu)
    if(IWYU_EXECUTABLE)
        set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${IWYU_EXECUTABLE})
    else()
        message(WARNING "include-what-you-use requested but not found")
    endif()
endif()

# Dependencies using FetchContent (Built-in CMake module)
include(FetchContent)

# 1. nlohmann/json - Industry standard for JSON in C++
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# 2. Catch2 - Modern BDD-style testing framework
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Catch2)

# 3. tl::expected - std::expected backport
FetchContent_Declare(
    tl_expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG        v1.1.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tl_expected)
if(NOT TARGET tl_expected)
    add_library(tl_expected INTERFACE)
    target_include_directories(tl_expected INTERFACE ${tl_expected_SOURCE_DIR}/include)
endif()

# 4. Standalone ASIO - async networking primitives
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(asio)
if(NOT TARGET asio)
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
endif()

# 5. simdjson - SIMD-accelerated JSON parsing (2-10x faster)
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.9.4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(simdjson)

# 6. ada-url - Fast WHATWG-compliant URL parser (used by Node.js)
FetchContent_Declare(
    ada
    GIT_REPOSITORY https://github.com/ada-url/ada.git
    GIT_TAG        v2.9.2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(ADA_TESTING OFF CACHE BOOL "" FORCE)
set(ADA_BENCHMARKS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ada)

# 7. cpr - Modern C++ HTTP client (libcurl wrapper)
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG        1.10.5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpr)

# 8. spdlog - Fast C++ logging library
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# Main Library
add_library(mcpp_lib)
target_include_directories(mcpp_lib PUBLIC include)
target_sources(mcpp_lib PRIVATE
    src/protocol/json_rpc.cpp
    src/transport/stdio_transport.cpp
    src/transport/sse_parser.cpp
    src/transport/http_types.cpp
    src/transport/http_transport_config.cpp
    src/transport/http_transport.cpp
    src/transport/http_client_cpr.cpp
    src/transport/session_manager.cpp
    src/log/logger.cpp
    src/log/spdlog_logger.cpp
    src/json/fast_json.cpp
    src/client/mcp_client.cpp
    src/client/arcade_client.cpp
    src/transport/process_transport.cpp
    src/async/async_process_transport.cpp
    src/async/async_mcp_client.cpp
    src/security/url_validator.cpp
    src/resilience/circuit_breaker.cpp
)
target_link_libraries(mcpp_lib
    PUBLIC
        nlohmann_json::nlohmann_json
        tl_expected
        asio
        simdjson
        ada
        cpr::cpr
        spdlog::spdlog
)

# Enable warnings (Best Practice: Turn on strict warnings)
if(MSVC)
    target_compile_options(mcpp_lib PRIVATE /W4 /WX)
else()
    target_compile_options(mcpp_lib PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Test Executable
enable_testing()
add_executable(mcpp_tests
    tests/protocol_test.cpp
    tests/http_config_test.cpp
    tests/http_transport_test.cpp
    tests/json_rpc_request_test.cpp
    tests/stdio_transport_test.cpp
    tests/sse_parser_test.cpp
    tests/logger_test.cpp
    tests/fast_json_test.cpp
    tests/session_management_test.cpp
    tests/retry_test.cpp
    tests/integration_test.cpp
    tests/mcp_types_test.cpp
    tests/mcp_client_test.cpp
    tests/real_server_test.cpp
    tests/everything_server_test.cpp
    tests/process_transport_test.cpp
    tests/async_client_test.cpp
    tests/url_validator_test.cpp
    tests/handler_integration_test.cpp
    tests/additional_features_test.cpp
    tests/circuit_breaker_test.cpp
    tests/async_process_stderr_test.cpp
    tests/spdlog_logger_test.cpp
    tests/code_review_fixes_test.cpp
    tests/arcade_client_test.cpp
    tests/arcade_gateway_test.cpp
)
target_include_directories(mcpp_tests PRIVATE tests)
target_link_libraries(mcpp_tests PRIVATE mcpp_lib Catch2::Catch2WithMain)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(mcpp_tests)

# ═══════════════════════════════════════════════════════════════════════════
# CLI Tool
# ═══════════════════════════════════════════════════════════════════════════

# cxxopts - Lightweight C++ command line option parser
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.2.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cxxopts)

add_executable(mcpp-cli tools/mcpp-cli/main.cpp)
target_link_libraries(mcpp-cli PRIVATE mcpp_lib cxxopts::cxxopts)

# Install CLI tool
install(TARGETS mcpp-cli RUNTIME DESTINATION bin)

