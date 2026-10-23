// bridge/hook_manager.cpp
#include "bridge/hook_manager.h"
#include "art/art_guard.h"
#include "core/message_router.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace bridge {

HookManager& HookManager::instance() {
    static HookManager m;
    return m;
}

HookManager::HookManager() {
    MessageRouter::instance().subscribe("hook.enter",
        [this](const nlohmann::json& msg) { this->on_hook_enter(msg); });
    MessageRouter::instance().subscribe("hook.leave",
        [this](const nlohmann::json& msg) { this->on_hook_leave(msg); });
    subscribed_ = true;
}

uint64_t HookManager::install(const std::string& className,
                              const std::string& methodName,
                              const std::vector<std::string>& overloadTypes,
                              EnterCallback onEnter,
                              LeaveCallback onLeave,
                              std::string& error,
                              int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return 0;
    }
    if (className.empty()) { error = "className required"; return 0; }
    if (methodName.empty()) { error = "methodName required"; return 0; }

    nlohmann::json args = {
        {"className",     className},
        {"methodName",    methodName},
        {"overloadTypes", overloadTypes}
    };
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("bridge.hook.install",
                                                args, result, error, timeout_ms)) {
        return 0;
    }
    if (!result.value("ok", false)) {
        error = result.value("error", std::string("install returned not-ok"));
        return 0;
    }
    uint64_t hookId = result.value("hookId", (uint64_t)0);
    if (hookId == 0) {
        error = "install returned zero hook id";
        return 0;
    }

    Entry e;
    e.rec.hookId        = hookId;
    e.rec.className     = className;
    e.rec.methodName    = methodName;
    e.rec.overloadTypes = overloadTypes;
    e.on_enter          = std::move(onEnter);
    e.on_leave          = std::move(onLeave);

    {
        std::lock_guard<std::mutex> lk(mu_);
        entries_[hookId] = std::move(e);
    }
    return hookId;
}

bool HookManager::uninstall(uint64_t hookId, std::string& error, int timeout_ms) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (entries_.find(hookId) == entries_.end()) {
            error = "no hook with that id";
            return false;
        }
    }
    nlohmann::json args = {{"hookId", hookId}};
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("bridge.hook.uninstall",
                                                args, result, error, timeout_ms)) {
        return false;
    }
    if (!result.value("ok", false)) {
        error = result.value("error", std::string("uninstall returned not-ok"));
        return false;
    }
    std::lock_guard<std::mutex> lk(mu_);
    entries_.erase(hookId);
    return true;
}

std::vector<HookRecord> HookManager::list() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<HookRecord> out;
    out.reserve(entries_.size());
    for (const auto& kv : entries_) out.push_back(kv.second.rec);
    return out;
}

void HookManager::release_all() {
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& kv : entries_) ids.push_back(kv.first);
    }
    for (auto id : ids) {
        std::string err;
        uninstall(id, err, 1000);
    }
}

void HookManager::set_global_enter_callback(EnterCallback cb) {
    std::lock_guard<std::mutex> lk(mu_);
    global_enter_ = std::move(cb);
}

void HookManager::set_global_leave_callback(LeaveCallback cb) {
    std::lock_guard<std::mutex> lk(mu_);
    global_leave_ = std::move(cb);
}

void HookManager::on_hook_enter(const nlohmann::json& msg) {
    if (!msg.contains("payload")) return;
    const auto& p = msg["payload"];

    HookEnterEvent ev;
    ev.hookId     = p.value("hookId", (uint64_t)0);
    ev.className  = p.value("className", std::string());
    ev.methodName = p.value("methodName", std::string());
    ev.threadId   = p.value("threadId", (uint32_t)0);
    if (p.contains("args")) ev.args = p["args"];

    EnterCallback specific;
    EnterCallback global;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = entries_.find(ev.hookId);
        if (it != entries_.end()) specific = it->second.on_enter;
        global = global_enter_;
    }
    if (specific) { try { specific(ev); } catch (...) {} }
    if (global)   { try { global(ev); }   catch (...) {} }
}

void HookManager::on_hook_leave(const nlohmann::json& msg) {
    if (!msg.contains("payload")) return;
    const auto& p = msg["payload"];

    HookLeaveEvent ev;
    ev.hookId      = p.value("hookId", (uint64_t)0);
    ev.className   = p.value("className", std::string());
    ev.methodName  = p.value("methodName", std::string());
    ev.threw       = p.value("threw", false);
    ev.error       = p.value("error", std::string());
    if (p.contains("returnValue")) ev.returnValue = p["returnValue"];

    LeaveCallback specific;
    LeaveCallback global;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = entries_.find(ev.hookId);
        if (it != entries_.end()) specific = it->second.on_leave;
        global = global_leave_;
    }
    if (specific) { try { specific(ev); } catch (...) {} }
    if (global)   { try { global(ev); }   catch (...) {} }
}

} // namespace bridge
} // namespace yamgg
