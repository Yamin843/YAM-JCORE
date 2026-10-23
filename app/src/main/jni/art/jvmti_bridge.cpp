// art/jvmti_bridge.cpp
#include "art/jvmti_bridge.h"
#include "art/art_guard.h"
#include "core/message_router.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace art {

JvmtiBridge& JvmtiBridge::instance() {
    static JvmtiBridge b;
    return b;
}

bool JvmtiBridge::add_capabilities(std::string& error, int timeout_ms) {
    if (!ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }

    nlohmann::json args = nlohmann::json::object();
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("jvmti.add_capabilities",
                                                args, result, error, timeout_ms)) {
        return false;
    }
    if (result.value("canTagObjects", false)) {
        return true;
    }
    error = result.value("error", std::string("add_capabilities returned false"));
    return false;
}

bool JvmtiBridge::get_loaded_classes(std::vector<LoadedClass>& out,
                                     std::string& error,
                                     int timeout_ms) {
    if (!ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }

    nlohmann::json args = nlohmann::json::object();
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("jvmti.get_loaded_classes",
                                                args, result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("classes") || !result["classes"].is_array()) {
        error = "malformed jvmti.get_loaded_classes reply";
        return false;
    }
    out.clear();
    out.reserve(result["classes"].size());
    for (const auto& c : result["classes"]) {
        if (c.is_string()) {
            out.push_back({c.get<std::string>()});
        }
    }
    return true;
}

bool JvmtiBridge::get_class_members(const std::string& className,
                                    ClassInspection& out,
                                    std::string& error,
                                    int timeout_ms) {
    if (!ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }

    nlohmann::json args = {{"className", className}};
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("jvmti.get_class_methods",
                                                args, result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("methods") || !result["methods"].is_array()) {
        error = "malformed jvmti.get_class_methods reply";
        return false;
    }

    out.className = result.value("className", className);
    out.methods.clear();
    out.fields.clear();

    for (const auto& m : result["methods"]) {
        ClassMethod cm;
        cm.name         = m.value("name", std::string());
        cm.returnType   = m.value("returnType", std::string());
        cm.type         = m.value("type", std::string());
        if (m.contains("argumentTypes") && m["argumentTypes"].is_array()) {
            for (const auto& a : m["argumentTypes"]) {
                if (a.is_string()) cm.argumentTypes.push_back(a.get<std::string>());
            }
        }
        out.methods.push_back(std::move(cm));
    }

    if (result.contains("fields") && result["fields"].is_array()) {
        for (const auto& f : result["fields"]) {
            ClassField cf;
            cf.name = f.value("name", std::string());
            cf.type = f.value("type", std::string());
            out.fields.push_back(std::move(cf));
        }
    }

    return true;
}

} // namespace art
} // namespace yamgg
