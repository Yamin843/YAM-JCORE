// art/art_spec.cpp
#include "art/art_spec.h"
#include "core/message_router.h"

#include <mutex>

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace art {

namespace {
struct Cache {
    std::mutex      mu;
    bool            method_valid   = false;
    bool            thread_valid   = false;
    bool            klass_valid    = false;
    bool            runtime_valid  = false;
    bool            cl_valid       = false;

    ArtMethodSpec   method;
    ArtThreadSpec   thread;
    ArtClassSpec    klass;
    ArtRuntimeSpec  runtime;
    ClassLinkerSpec cl;
};
Cache g_cache;
} // namespace

ArtSpec& ArtSpec::instance() {
    static ArtSpec s;
    return s;
}

void ArtSpec::invalidate() {
    std::lock_guard<std::mutex> lk(g_cache.mu);
    g_cache.method_valid  = false;
    g_cache.thread_valid  = false;
    g_cache.klass_valid   = false;
    g_cache.runtime_valid = false;
    g_cache.cl_valid      = false;
}

bool ArtSpec::method(ArtMethodSpec& out, std::string& error, int timeout_ms) {
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        if (g_cache.method_valid) { out = g_cache.method; return true; }
    }

    nlohmann::json result;
    if (!MessageRouter::instance().send_command("art_spec.method",
                                                nlohmann::json::object(),
                                                result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("api") || !result.contains("spec")) {
        error = "malformed art_spec.method reply";
        return false;
    }
    ArtMethodSpec s;
    s.api             = result["api"].get<int>();
    const auto& spec  = result["spec"];
    s.size            = spec.value("size", 0);
    s.jniCode         = spec.value("jniCode", 0);
    s.quickCode       = spec.value("quickCode", 0);
    s.accessFlags     = spec.value("accessFlags", 0);
    if (spec.contains("interpreterCode") && !spec["interpreterCode"].is_null()) {
        s.interpreterCode = spec["interpreterCode"].get<int>();
    } else {
        s.interpreterCode = -1;
    }

    if (s.api < 24) {
        LOGW("ArtSpec::method: API %d is below tested range (24..36)", s.api);
    }

    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        g_cache.method       = s;
        g_cache.method_valid = true;
    }
    out = s;
    return true;
}

bool ArtSpec::thread(ArtThreadSpec& out, std::string& error, int timeout_ms) {
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        if (g_cache.thread_valid) { out = g_cache.thread; return true; }
    }
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("art_spec.thread",
                                                nlohmann::json::object(),
                                                result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("api") || !result.contains("spec")) {
        error = "malformed art_spec.thread reply";
        return false;
    }
    ArtThreadSpec s;
    s.api             = result["api"].get<int>();
    const auto& spec  = result["spec"];
    s.self            = spec.value("self", 0);
    s.exception       = spec.value("exception", 0);
    s.throwLocation   = spec.value("throwLocation", 0);
    s.topHandleScope  = spec.value("topHandleScope", 0);
    s.managedStack    = spec.value("managedStack", 0);
    if (spec.contains("isExceptionReportedToInstrumentation") &&
        !spec["isExceptionReportedToInstrumentation"].is_null()) {
        s.isExceptionReportedToInstrumentation =
            spec["isExceptionReportedToInstrumentation"].get<int>();
    } else {
        s.isExceptionReportedToInstrumentation = -1;
    }
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        g_cache.thread       = s;
        g_cache.thread_valid = true;
    }
    out = s;
    return true;
}

bool ArtSpec::klass(ArtClassSpec& out, std::string& error, int timeout_ms) {
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        if (g_cache.klass_valid) { out = g_cache.klass; return true; }
    }
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("art_spec.class",
                                                nlohmann::json::object(),
                                                result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("api") || !result.contains("spec")) {
        error = "malformed art_spec.class reply";
        return false;
    }
    ArtClassSpec s;
    s.api                 = result["api"].get<int>();
    const auto& spec      = result["spec"];
    s.ifields             = spec.value("ifields", 0);
    s.methods             = spec.value("methods", 0);
    s.sfields             = spec.value("sfields", 0);
    s.copiedMethodsOffset = spec.value("copiedMethodsOffset", 0);
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        g_cache.klass       = s;
        g_cache.klass_valid = true;
    }
    out = s;
    return true;
}

bool ArtSpec::runtime(ArtRuntimeSpec& out, std::string& error, int timeout_ms) {
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        if (g_cache.runtime_valid) { out = g_cache.runtime; return true; }
    }
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("art_spec.runtime",
                                                nlohmann::json::object(),
                                                result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("api") || !result.contains("spec")) {
        error = "malformed art_spec.runtime reply";
        return false;
    }
    ArtRuntimeSpec s;
    s.api            = result["api"].get<int>();
    const auto& spec = result["spec"];
    s.heap           = spec.value("heap", 0);
    s.threadList     = spec.value("threadList", 0);
    s.internTable    = spec.value("internTable", 0);
    s.classLinker    = spec.value("classLinker", 0);
    s.jniIdManager   = spec.value("jniIdManager", 0);
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        g_cache.runtime       = s;
        g_cache.runtime_valid = true;
    }
    out = s;
    return true;
}

bool ArtSpec::class_linker(ClassLinkerSpec& out, std::string& error, int timeout_ms) {
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        if (g_cache.cl_valid) { out = g_cache.cl; return true; }
    }
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("art_spec.classLinker",
                                                nlohmann::json::object(),
                                                result, error, timeout_ms)) {
        return false;
    }
    if (!result.contains("api") || !result.contains("spec")) {
        error = "malformed art_spec.classLinker reply";
        return false;
    }
    ClassLinkerSpec s;
    s.api                                      = result["api"].get<int>();
    const auto& spec                           = result["spec"];
    s.quickResolutionTrampoline                = spec.value("quickResolutionTrampoline", 0);
    s.quickImtConflictTrampoline               = spec.value("quickImtConflictTrampoline", 0);
    s.quickGenericJniTrampoline                = spec.value("quickGenericJniTrampoline", 0);
    s.quickToInterpreterBridgeTrampoline       = spec.value("quickToInterpreterBridgeTrampoline", 0);
    {
        std::lock_guard<std::mutex> lk(g_cache.mu);
        g_cache.cl       = s;
        g_cache.cl_valid = true;
    }
    out = s;
    return true;
}

} // namespace art
} // namespace yamgg
