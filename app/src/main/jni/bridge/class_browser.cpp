// bridge/class_browser.cpp
#include "bridge/class_browser.h"
#include "art/art_guard.h"
#include "core/message_router.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace bridge {

ClassBrowser& ClassBrowser::instance() {
    static ClassBrowser b;
    return b;
}

bool ClassBrowser::list_classes(const std::string& filter,
                                std::vector<std::string>& out,
                                std::string& error,
                                int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }
    nlohmann::json args = nlohmann::json::object();
    if (!filter.empty()) args["filter"] = filter;
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("bridge.class.list",
                                                args, result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("classes") || !result["classes"].is_array()) {
        error = "malformed reply";
        return false;
    }
    out.clear();
    out.reserve(result["classes"].size());
    for (const auto& c : result["classes"]) {
        if (c.is_string()) out.push_back(c.get<std::string>());
    }
    return true;
}

bool ClassBrowser::inspect_class(const std::string& className,
                                 ClassInspection& out,
                                 std::string& error,
                                 bool force_refresh,
                                 int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }
    if (!force_refresh) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(className);
        if (it != cache_.end()) {
            out = it->second;
            return true;
        }
    }

    nlohmann::json args = {{"className", className}};
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("bridge.class.inspect",
                                                args, result, error, timeout_ms)) {
        return false;
    }

    ClassInspection info;
    info.className = result.value("className", className);

    if (result.contains("methods") && result["methods"].is_array()) {
        for (const auto& m : result["methods"]) {
            MethodSignature ms;
            ms.name        = m.value("name", std::string());
            ms.returnType  = m.value("returnType", std::string());
            ms.type        = m.value("type", std::string());
            if (m.contains("argumentTypes") && m["argumentTypes"].is_array()) {
                for (const auto& a : m["argumentTypes"]) {
                    if (a.is_string()) ms.argumentTypes.push_back(a.get<std::string>());
                }
            }
            info.methods.push_back(std::move(ms));
        }
    }
    if (result.contains("fields") && result["fields"].is_array()) {
        for (const auto& f : result["fields"]) {
            FieldSignature fs;
            fs.name = f.value("name", std::string());
            fs.type = f.value("type", std::string());
            info.fields.push_back(std::move(fs));
        }
    }
    if (result.contains("constructors") && result["constructors"].is_array()) {
        for (const auto& c : result["constructors"]) {
            ConstructorSignature cs;
            if (c.contains("argumentTypes") && c["argumentTypes"].is_array()) {
                for (const auto& a : c["argumentTypes"]) {
                    if (a.is_string()) cs.argumentTypes.push_back(a.get<std::string>());
                }
            }
            info.constructors.push_back(std::move(cs));
        }
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        cache_[className] = info;
    }
    out = std::move(info);
    return true;
}

void ClassBrowser::invalidate(const std::string& className) {
    std::lock_guard<std::mutex> lk(mu_);
    cache_.erase(className);
}

void ClassBrowser::invalidate_all() {
    std::lock_guard<std::mutex> lk(mu_);
    cache_.clear();
}

} // namespace bridge
} // namespace yamgg
