// bridge/call_engine.cpp
#include "bridge/call_engine.h"
#include "bridge/handle_registry.h"
#include "art/art_guard.h"
#include "core/message_router.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace bridge {

CallEngine& CallEngine::instance() {
    static CallEngine e;
    return e;
}

bool CallEngine::call_instance(const std::string& className,
                               const std::string& methodName,
                               const std::vector<std::string>& overloadTypes,
                               const std::string& thisHandle,
                               const nlohmann::json& args,
                               nlohmann::json& result,
                               std::string& error,
                               int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }
    if (className.empty())  { error = "className required";  return false; }
    if (methodName.empty()) { error = "methodName required"; return false; }
    if (thisHandle.empty()) { error = "thisHandle required"; return false; }

    nlohmann::json argsObj = {
        {"className",     className},
        {"methodName",    methodName},
        {"overloadTypes", overloadTypes},
        {"thisHandle",    thisHandle},
        {"args",          args.is_array() ? args : nlohmann::json::array()}
    };
    nlohmann::json reply;
    if (!MessageRouter::instance().send_command("bridge.call.instance",
                                                argsObj, reply, error, timeout_ms)) {
        return false;
    }
    if (!reply.value("ok", false)) {
        error = reply.value("error", std::string("call returned not-ok"));
        return false;
    }
    result = reply.contains("returnValue") ? reply["returnValue"] : nlohmann::json();
    HandleRegistry::instance().touch(thisHandle);
    return true;
}

bool CallEngine::call_static(const std::string& className,
                             const std::string& methodName,
                             const std::vector<std::string>& overloadTypes,
                             const nlohmann::json& args,
                             nlohmann::json& result,
                             std::string& error,
                             int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }
    if (className.empty())  { error = "className required";  return false; }
    if (methodName.empty()) { error = "methodName required"; return false; }

    nlohmann::json argsObj = {
        {"className",     className},
        {"methodName",    methodName},
        {"overloadTypes", overloadTypes},
        {"args",          args.is_array() ? args : nlohmann::json::array()}
    };
    nlohmann::json reply;
    if (!MessageRouter::instance().send_command("bridge.call.static",
                                                argsObj, reply, error, timeout_ms)) {
        return false;
    }
    if (!reply.value("ok", false)) {
        error = reply.value("error", std::string("call returned not-ok"));
        return false;
    }
    result = reply.contains("returnValue") ? reply["returnValue"] : nlohmann::json();
    return true;
}

bool CallEngine::create_instance(const std::string& className,
                                 const std::vector<std::string>& ctorTypes,
                                 const nlohmann::json& args,
                                 std::string& handle,
                                 std::string& error,
                                 int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }
    if (className.empty()) { error = "className required"; return false; }

    nlohmann::json argsObj = {
        {"className", className},
        {"ctorTypes", ctorTypes},
        {"args",      args.is_array() ? args : nlohmann::json::array()}
    };
    nlohmann::json reply;
    if (!MessageRouter::instance().send_command("bridge.call.create",
                                                argsObj, reply, error, timeout_ms)) {
        return false;
    }
    if (!reply.value("ok", false)) {
        error = reply.value("error", std::string("create returned not-ok"));
        return false;
    }
    std::string h = reply.value("handle", std::string());
    if (h.empty()) {
        error = "create returned empty handle";
        return false;
    }
    HandleRegistry::instance().register_handle(h, className);
    handle = h;
    return true;
}

bool CallEngine::read_field(const std::string& className,
                            const std::string& fieldName,
                            const std::string& thisHandle,
                            nlohmann::json& value,
                            std::string& error,
                            int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }
    if (className.empty()) { error = "className required"; return false; }
    if (fieldName.empty()) { error = "fieldName required"; return false; }

    nlohmann::json argsObj = {
        {"className", className},
        {"fieldName", fieldName},
        {"thisHandle", thisHandle}
    };
    nlohmann::json reply;
    if (!MessageRouter::instance().send_command("bridge.field.read",
                                                argsObj, reply, error, timeout_ms)) {
        return false;
    }
    if (!reply.value("ok", false)) {
        error = reply.value("error", std::string("read_field returned not-ok"));
        return false;
    }
    value = reply.contains("value") ? reply["value"] : nlohmann::json();
    if (!thisHandle.empty()) HandleRegistry::instance().touch(thisHandle);
    return true;
}

bool CallEngine::write_field(const std::string& className,
                             const std::string& fieldName,
                             const std::string& thisHandle,
                             const nlohmann::json& value,
                             std::string& error,
                             int timeout_ms) {
    if (!art::ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }
    if (className.empty()) { error = "className required"; return false; }
    if (fieldName.empty()) { error = "fieldName required"; return false; }

    nlohmann::json argsObj = {
        {"className", className},
        {"fieldName", fieldName},
        {"thisHandle", thisHandle},
        {"value",     value}
    };
    nlohmann::json reply;
    if (!MessageRouter::instance().send_command("bridge.field.write",
                                                argsObj, reply, error, timeout_ms)) {
        return false;
    }
    if (!reply.value("ok", false)) {
        error = reply.value("error", std::string("write_field returned not-ok"));
        return false;
    }
    if (!thisHandle.empty()) HandleRegistry::instance().touch(thisHandle);
    return true;
}

} // namespace bridge
} // namespace yamgg
