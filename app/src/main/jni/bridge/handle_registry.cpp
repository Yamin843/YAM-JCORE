// bridge/handle_registry.cpp
#include "bridge/handle_registry.h"
#include "core/message_router.h"

#include <vector>
#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace bridge {

HandleRegistry& HandleRegistry::instance() {
    static HandleRegistry r;
    return r;
}

void HandleRegistry::set_capacity(size_t cap) {
    std::lock_guard<std::mutex> lk(mu_);
    capacity_ = (cap < 16) ? 16 : cap;
}

size_t HandleRegistry::capacity() const {
    std::lock_guard<std::mutex> lk(mu_);
    return capacity_;
}

void HandleRegistry::register_handle(const std::string& handle,
                                     const std::string& className) {
    std::vector<std::string> to_release;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = lru_pos_.find(handle);
        if (it != lru_pos_.end()) {
            lru_.erase(it->second);
            lru_pos_.erase(it);
        }
        lru_.push_back(handle);
        lru_pos_[handle] = std::prev(lru_.end());
        class_by_handle_[handle] = className;
        enforce_capacity_locked(to_release);
    }

    if (!to_release.empty()) {
        for (const auto& h : to_release) {
            std::string err;
            release(h, err, 2000);
        }
    }
}

bool HandleRegistry::lookup(const std::string& handle, std::string& className) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = class_by_handle_.find(handle);
    if (it == class_by_handle_.end()) return false;
    className = it->second;
    return true;
}

void HandleRegistry::touch(const std::string& handle) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = lru_pos_.find(handle);
    if (it == lru_pos_.end()) return;
    lru_.erase(it->second);
    lru_.push_back(handle);
    it->second = std::prev(lru_.end());
}

bool HandleRegistry::release(const std::string& handle,
                             std::string& error,
                             int timeout_ms) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = class_by_handle_.find(handle);
        if (it == class_by_handle_.end()) return false;
        class_by_handle_.erase(it);
        auto lit = lru_pos_.find(handle);
        if (lit != lru_pos_.end()) {
            lru_.erase(lit->second);
            lru_pos_.erase(lit);
        }
    }

    nlohmann::json args = {{"handle", handle}};
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("bridge.handle.release",
                                                args, result, error, timeout_ms)) {
        return false;
    }
    return result.value("ok", false);
}

void HandleRegistry::release_all() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        class_by_handle_.clear();
        lru_.clear();
        lru_pos_.clear();
    }
    nlohmann::json args = nlohmann::json::object();
    nlohmann::json result;
    std::string err;
    MessageRouter::instance().send_command("bridge.handle.releaseAll",
                                           args, result, err, 3000);
}

size_t HandleRegistry::count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return class_by_handle_.size();
}

void HandleRegistry::enforce_capacity_locked(std::vector<std::string>& to_release) {
    while (class_by_handle_.size() > capacity_ && !lru_.empty()) {
        to_release.push_back(lru_.front());
        lru_.pop_front();
        lru_pos_.erase(to_release.back());
        class_by_handle_.erase(to_release.back());
    }
}

} // namespace bridge
} // namespace yamgg
