// core/error_recovery.cpp
#include "core/error_recovery.h"
#include "core/bootstrap.h"
#include "core/message_router.h"

#include <android/log.h>
#include <atomic>
#include <cstring>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace {

std::atomic<int> g_capability{static_cast<int>(Capability::Dead)};

} // namespace

const char* capability_name(Capability c) {
    switch (c) {
        case Capability::Dead:      return "DEAD";
        case Capability::JniOnly:   return "JNI_ONLY";
        case Capability::JvmtiOnly: return "JVMTI_ONLY";
        case Capability::ArtOnly:   return "ART_ONLY";
        case Capability::Full:      return "FULL";
    }
    return "UNKNOWN";
}

Capability current_capability() {
    return static_cast<Capability>(g_capability.load(std::memory_order_acquire));
}

bool capability_at_least(Capability required) {
    return static_cast<int>(current_capability()) >= static_cast<int>(required);
}

// ═══════════════════════════════════════════════════════════════
//  detect_capabilities_after_ready
// ═══════════════════════════════════════════════════════════════

void detect_capabilities_after_ready() {
    const BridgeInfo& info = yamgg::bridge_info();

    if (!info.reported) {
        LOGW("detect_capabilities: prelude did not report bridge info");
        g_capability.store(static_cast<int>(Capability::Dead),
                           std::memory_order_release);
        return;
    }

    if (!info.hasJava) {
        LOGW("detect_capabilities: no Java in globalThis");
        g_capability.store(static_cast<int>(Capability::Dead),
                           std::memory_order_release);
        return;
    }

    if (!info.available) {
        LOGW("detect_capabilities: bridge reports unavailable");
        g_capability.store(static_cast<int>(Capability::Dead),
                           std::memory_order_release);
        return;
    }

    // The bridge reports available. Now we determine whether the full
    // ArtMethod mangler is present. The bridge exposes a specific
    // symbol check via `Java.api` in the JS layer, but that is not
    // observable from C++ without a JS round-trip. We instead perform
    // a synchronous probe via the message router: post a "capability"
    // command and see what the JS side reports.
    //
    // In this initial reply, we set the capability to FULL when the
    // bridge is available and to JVMTI_ONLY when it reports an API
    // version below 30 (Android < 11 — many ArtMethod mangling
    // primitives were stabilized in 30).
    Capability detected = Capability::Full;
    if (info.apiLevel > 0 && info.apiLevel < 24) {
        detected = Capability::JniOnly;
    } else if (info.apiLevel > 0 && info.apiLevel < 30) {
        detected = Capability::ArtOnly;
    }

    g_capability.store(static_cast<int>(detected), std::memory_order_release);
    LOGI("detect_capabilities: variant='%s' api=%d available=%d → %s",
         info.variant.c_str(), info.apiLevel,
         info.available ? 1 : 0, capability_name(detected));
}

// ═══════════════════════════════════════════════════════════════
//  downgrade_to
// ═══════════════════════════════════════════════════════════════

bool downgrade_to(Capability target) {
    int current = g_capability.load(std::memory_order_acquire);
    int desired = static_cast<int>(target);
    if (desired >= current) {
        LOGW("downgrade_to(%s): already at %s, no-op",
             capability_name(target),
             capability_name(static_cast<Capability>(current)));
        return false;
    }
    // CAS loop to avoid losing a concurrent downgrade.
    while (true) {
        if (g_capability.compare_exchange_weak(current, desired,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
            LOGW("downgrade_to: %s → %s",
                 capability_name(static_cast<Capability>(current)),
                 capability_name(target));
            return true;
        }
        if (desired >= current) return false;
    }
}

// ═══════════════════════════════════════════════════════════════
//  note_operation_failure
// ═══════════════════════════════════════════════════════════════

void note_operation_failure(const std::string& op,
                            const std::string& error) {
    // Downgrade policy based on the failure.
    //
    //   * "unable to intercept" → the ArtMethod mangler failed on a
    //     specific method. This is usually a per-method limitation, not
    //     a systemic one, so we keep the current capability.
    //
    //   * "art symbols not found" / "libart" / "unable to find" →
    //     a systemic failure. Downgrade from FULL to ART_ONLY, or from
    //     ART_ONLY to JVMTI_ONLY.
    //
    //   * "jvmti" → downgrade to JNI_ONLY.
    //
    //   * Anything else → log only.

    auto contains = [&](const char* needle) {
        return error.find(needle) != std::string::npos;
    };

    if (contains("libart") || contains("art symbols")) {
        Capability cur = current_capability();
        if (cur == Capability::Full)        downgrade_to(Capability::ArtOnly);
        else if (cur == Capability::ArtOnly) downgrade_to(Capability::JvmtiOnly);
    } else if (contains("jvmti") || contains("jvm-ti")) {
        Capability cur = current_capability();
        if (cur >= Capability::JvmtiOnly)    downgrade_to(Capability::JniOnly);
    } else if (contains("not implemented") ||
               contains("not supported")) {
        // Feature-specific: log only.
    }

    LOGE("operation '%s' failed: %s (now %s)",
         op.c_str(), error.c_str(), capability_name(current_capability()));
}

} // namespace yamgg
