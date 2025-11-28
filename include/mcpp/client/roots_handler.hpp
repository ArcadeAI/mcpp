#ifndef MCPP_CLIENT_ROOTS_HANDLER_HPP
#define MCPP_CLIENT_ROOTS_HANDLER_HPP

#include "mcpp/protocol/mcp_types.hpp"

#include <mutex>

namespace mcpp {

// ═══════════════════════════════════════════════════════════════════════════
// Roots Handler Interface
// ═══════════════════════════════════════════════════════════════════════════
//
// Implement this interface to provide filesystem root information to MCP servers.
// Roots define the boundaries of what directories/files servers can access.
//
// Example Usage:
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                                                                         │
// │  // 1. Implement the handler                                            │
// │  class MyWorkspaceRoots : public IRootsHandler {                        │
// │  public:                                                                │
// │      ListRootsResult list_roots() override {                            │
// │          return ListRootsResult{{                                       │
// │              Root{"file:///home/user/project", "Current Project"},      │
// │              Root{"file:///home/user/shared", "Shared Libraries"}       │
// │          }};                                                            │
// │      }                                                                  │
// │  };                                                                     │
// │                                                                         │
// │  // 2. Register with client                                             │
// │  auto handler = std::make_shared<MyWorkspaceRoots>();                   │
// │  client.set_roots_handler(handler);                                     │
// │                                                                         │
// │  // 3. When roots change, notify the server                             │
// │  client.notify_roots_changed();                                         │
// │                                                                         │
// └─────────────────────────────────────────────────────────────────────────┘

class IRootsHandler {
public:
    virtual ~IRootsHandler() = default;

    // ───────────────────────────────────────────────────────────────────────
    // List Roots Handler
    // ───────────────────────────────────────────────────────────────────────
    // Called when a server requests the list of available roots.
    //
    // Returns:
    //   ListRootsResult containing all accessible filesystem roots
    //
    // Implementation Notes:
    //   - Return file:// URIs for local filesystem paths
    //   - Include human-readable names for better UX
    //   - Can return empty list if no roots are available
    //   - Roots should be absolute paths
    //
    [[nodiscard]] virtual ListRootsResult list_roots() = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Static Roots Handler
// ═══════════════════════════════════════════════════════════════════════════
// Returns a fixed list of roots configured at construction time.
// Use when roots don't change during the client's lifetime.

class StaticRootsHandler : public IRootsHandler {
public:
    explicit StaticRootsHandler(std::vector<Root> roots)
        : roots_(std::move(roots)) {}

    [[nodiscard]] ListRootsResult list_roots() override {
        return ListRootsResult{roots_};
    }

private:
    std::vector<Root> roots_;
};

// ═══════════════════════════════════════════════════════════════════════════
// Mutable Roots Handler
// ═══════════════════════════════════════════════════════════════════════════
// Allows roots to be updated dynamically. Thread-safe for concurrent access.
// 
// Note: After calling set_roots() or add_root(), you should call
// client.notify_roots_changed() to inform connected servers.

class MutableRootsHandler : public IRootsHandler {
public:
    MutableRootsHandler() = default;
    explicit MutableRootsHandler(std::vector<Root> initial_roots)
        : roots_(std::move(initial_roots)) {}

    [[nodiscard]] ListRootsResult list_roots() override {
        std::lock_guard lock(mutex_);
        return ListRootsResult{roots_};
    }

    void set_roots(std::vector<Root> roots) {
        std::lock_guard lock(mutex_);
        roots_ = std::move(roots);
    }

    void add_root(Root root) {
        std::lock_guard lock(mutex_);
        roots_.push_back(std::move(root));
    }

    void clear_roots() {
        std::lock_guard lock(mutex_);
        roots_.clear();
    }

    [[nodiscard]] std::size_t root_count() const {
        std::lock_guard lock(mutex_);
        return roots_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<Root> roots_;
};

}  // namespace mcpp

#endif  // MCPP_CLIENT_ROOTS_HANDLER_HPP

