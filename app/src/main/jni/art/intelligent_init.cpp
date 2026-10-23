// art/intelligent_init.cpp
#include "art/intelligent_init.h"

#include <android/log.h>

#include <chrono>
#include <mutex>
#include <thread>

#include "art/art_guard.h"
#include "core/bootstrap.h"
#include "core/message_router.h"
#include "native/java_bridge.h"
#include "native/runtime.h"

#define LOG_TAG "YAMGG-IntelInit"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace art {

namespace {

std::mutex              g_mu;
IntelligentInitState    g_state;
std::mutex              g_ready_mu;
std::condition_variable g_ready_cv;
bool                    g_ready_signal = false;
bool                    g_ready_result = false;
std::string             g_ready_reason;

void on_smart_init_event(const nlohmann::json& msg) {
    if (!msg.contains("name")) return;
    const std::string& name = msg["name"].get<std::string>();
    if (name != "smart_init.ready" && name != "smart_init.failed") return;

    bool ok = (name == "smart_init.ready");
    std::string reason;
    std::string libartPath;
    int apiLevel = 0;
    if (msg.contains("payload") && msg["payload"].is_object()) {
        const auto& p = msg["payload"];
        reason     = p.value("reason", std::string());
        libartPath = p.value("libartPath", std::string());
        apiLevel   = p.value("apiLevel", 0);
    }

    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_state.bridge_available = ok;
        g_state.libartPath       = libartPath;
        g_state.apiLevel         = apiLevel;
        if (!ok) g_state.reason  = reason;
    }

    std::lock_guard<std::mutex> lk(g_ready_mu);
    g_ready_signal = true;
    g_ready_result = ok;
    g_ready_reason = reason;
    g_ready_cv.notify_all();
}

bool start_js_smart_init(int timeout_ms) {
    nlohmann::json args = {{"timeoutMs", timeout_ms}};
    nlohmann::json result;
    std::string err;

    // Fire-and-forget; the outcome arrives as events.
    MessageRouter::instance().post_command(
        "smart_init.start", args,
        [](bool /*ok*/, const nlohmann::json& /*reply*/) {
            // Intentionally empty — the outcome is delivered via
            // smart_init.ready / smart_init.failed events.
        });

    // Subscribe to those events. Idempotent — safe to install here.
    static bool installed = false;
    if (!installed) {
        installed = true;
        MessageRouter::instance().subscribe("smart_init.ready",
            [](const nlohmann::json& msg) { on_smart_init_event(msg); });
        MessageRouter::instance().subscribe("smart_init.failed",
            [](const nlohmann::json& msg) { on_smart_init_event(msg); });
    }
    return true;
}

bool wait_for_smart_init(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms + 2000);
    std::unique_lock<std::mutex> lk(g_ready_mu);
    return g_ready_cv.wait_until(lk, deadline, [] { return g_ready_signal; });
}

bool try_native_jni_fallback(JavaVM* vm) {
    if (!native::JavaBridge::instance().init(vm)) {
        LOGE("native::JavaBridge::init failed");
        return false;
    }
    // Verify by testing a basic JNI call — find the Throwable class.
    JNIEnv* env = native::JavaBridge::instance().env();
    if (env == nullptr) {
        LOGE("native::JavaBridge::env() returned null");
        return false;
    }
    jclass thr = native::JavaBridge::instance().find_class("java/lang/Throwable");
    if (thr == nullptr) {
        LOGE("native::JavaBridge could not resolve java.lang.Throwable");
        return false;
    }
    LOGI("native::JavaBridge fallback verified (Throwable resolved)");
    return true;
}

} // namespace

bool intelligent_initialize(JavaVM* vm) {
    LOGI("=== intelligent_initialize begin ===");

    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_state = IntelligentInitState{};
    }

    // ── Phase A: JS bridge with smart_init ────────────────────
    LOGI("Phase A: starting JS smart_init (timeout 30s)");
    start_js_smart_init(30000);

    bool got_signal = wait_for_smart_init(35000);
    bool bridge_ok = false;
    if (!got_signal) {
        LOGW("smart_init timeout");
    } else {
        std::lock_guard<std::mutex> lk(g_ready_mu);
        bridge_ok = g_ready_result;
        if (!bridge_ok) {
            LOGW("smart_init failed: %s", g_ready_reason.c_str());
        }
    }

    if (bridge_ok) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_state.mode = IntelligentMode::Full;
        g_state.bridge_available = true;
        LOGI("✓ Phase A succeeded — Full mode (libart=%s)",
             g_state.libartPath.c_str());
        return true;
    }

    // ── Phase B: direct native JNI fallback ──────────────────
    LOGI("Phase B: trying direct native JNI fallback");
    if (try_native_jni_fallback(vm)) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_state.mode = IntelligentMode::JniOnly;
        g_state.native_jni_available = true;
        if (g_state.reason.empty()) {
            g_state.reason = "bridge_failed_native_works";
        }
        LOGI("✓ Phase B succeeded — JNI-only mode");
        return true;
    }

    // ── Both failed ─────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_state.mode = IntelligentMode::Dead;
        if (g_state.reason.empty()) {
            g_state.reason = "both_bridge_and_native_failed";
        }
    }
    LOGE("✗ Both phases failed — Dead mode");
    return false;
}

const IntelligentInitState& intelligent_state() {
    return g_state;
}

bool intelligent_is_usable() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_state.mode == IntelligentMode::Full ||
           g_state.mode == IntelligentMode::JniOnly;
}

} // namespace art
} // namespace yamgg
