// bridge/hook_manager.h — attach/detach/report Java method hooks.
#ifndef YAMGG_BRIDGE_HOOK_MANAGER_H
#define YAMGG_BRIDGE_HOOK_MANAGER_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace yamgg {
namespace bridge {

struct HookRecord {
    uint64_t                 hookId = 0;
    std::string              className;
    std::string              methodName;
    std::vector<std::string> overloadTypes;
};

struct HookEnterEvent {
    uint64_t       hookId = 0;
    std::string    className;
    std::string    methodName;
    nlohmann::json args;
    uint32_t       threadId = 0;
};

struct HookLeaveEvent {
    uint64_t       hookId = 0;
    std::string    className;
    std::string    methodName;
    nlohmann::json returnValue;
    bool           threw = false;
    std::string    error;
};

class HookManager {
public:
    using EnterCallback = std::function<void(const HookEnterEvent&)>;
    using LeaveCallback = std::function<void(const HookLeaveEvent&)>;

    static HookManager& instance();

    // Install a hook. Returns the hookId on success, 0 on failure
    // (error string set). Registers callbacks that fire from the JS
    // thread — callers MUST marshal to the UI thread if they touch
    // ImGui.
    uint64_t install(const std::string& className,
                     const std::string& methodName,
                     const std::vector<std::string>& overloadTypes,
                     EnterCallback onEnter,
                     LeaveCallback onLeave,
                     std::string& error,
                     int timeout_ms = 5000);

    // Uninstall a hook by id. Returns true on success.
    bool uninstall(uint64_t hookId, std::string& error, int timeout_ms = 3000);

    // List currently installed hooks.
    std::vector<HookRecord> list() const;

    // Release all hooks (called at session shutdown).
    void release_all();

    // Register callbacks that receive enter/leave events for hooks
    // installed elsewhere. Optional; if not set, HookManager uses the
    // per-hook callbacks registered at install time.
    void set_global_enter_callback(EnterCallback cb);
    void set_global_leave_callback(LeaveCallback cb);

private:
    HookManager();
    ~HookManager() = default;

    struct Entry {
        HookRecord   rec;
        EnterCallback on_enter;
        LeaveCallback on_leave;
    };

    void on_hook_enter(const nlohmann::json& msg);
    void on_hook_leave(const nlohmann::json& msg);

    mutable std::mutex                        mu_;
    std::unordered_map<uint64_t, Entry>       entries_;
    EnterCallback                             global_enter_;
    LeaveCallback                             global_leave_;
    bool                                      subscribed_ = false;
};

} // namespace bridge
} // namespace yamgg

#endif // YAMGG_BRIDGE_HOOK_MANAGER_H
