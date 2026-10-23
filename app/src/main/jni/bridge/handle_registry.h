// bridge/handle_registry.h — LRU cache for Java object handles.
//
// A "handle" is the string representation of a Java global-reference
// pointer. The JS side retains objects via Java.retain(), which returns
// a wrapper whose $h is the global-ref pointer. The string form of that
// pointer is what we pass around as a handle.
//
// The registry caps the number of live handles (default 1024). When the
// cap is reached, the least-recently-touched handle is released through
// bridge.handle.release so the corresponding global ref is freed.
#ifndef YAMGG_BRIDGE_HANDLE_REGISTRY_H
#define YAMGG_BRIDGE_HANDLE_REGISTRY_H

#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace yamgg {
namespace bridge {

class HandleRegistry {
public:
    static HandleRegistry& instance();

    // Configuration. Changing the cap does not evict immediately; it
    // affects future inserts.
    void set_capacity(size_t cap);
    size_t capacity() const;

    // Register a handle that JS already retained. The className is
    // cached for display purposes. Inserting the same handle twice
    // refreshes its tick.
    void register_handle(const std::string& handle, const std::string& className);

    // Look up a handle. Fills className if present.
    bool lookup(const std::string& handle, std::string& className) const;

    // Refresh the tick for LRU purposes.
    void touch(const std::string& handle);

    // Release a specific handle. Posts bridge.handle.release to JS.
    // Returns true if the handle existed.
    bool release(const std::string& handle, std::string& error, int timeout_ms = 3000);

    // Release all handles. Posts bridge.handle.releaseAll once.
    void release_all();

    // Number of live handles.
    size_t count() const;

private:
    HandleRegistry() = default;

    // Called with mu_ held. If count > capacity, releases the oldest
    // handles. The actual JS command runs outside the lock.
    void enforce_capacity_locked(std::vector<std::string>& to_release);

    mutable std::mutex                              mu_;
    std::unordered_map<std::string, std::string>    class_by_handle_;
    std::list<std::string>                          lru_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_pos_;
    size_t                                          capacity_ = 1024;
};

} // namespace bridge
} // namespace yamgg

#endif // YAMGG_BRIDGE_HANDLE_REGISTRY_H
