// art/art_guard.cpp
#include "art/art_guard.h"

#include <atomic>
#include <mutex>

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace art {

namespace {
struct GuardState {
    mutable std::mutex mu;
    bool               initialized  = false;
    BridgeCapabilities bridge;
    JvmtiCapabilities  jvmti;
    ArtCapabilities    art;
};
GuardState g_state;
} // namespace

ArtGuard& ArtGuard::instance() {
    static ArtGuard g;
    return g;
}

void ArtGuard::update(const BridgeCapabilities& bridge,
                     const JvmtiCapabilities&  jvmti,
                     const ArtCapabilities&    art) {
    std::lock_guard<std::mutex> lk(g_state.mu);
    g_state.bridge      = bridge;
    g_state.jvmti       = jvmti;
    g_state.art         = art;
    g_state.initialized = true;

    LOGI("ArtGuard: variant='%s' api=%d available=%d jvmti=%d artInternals=%d flavor='%s'",
         g_state.bridge.variant.c_str(),
         g_state.bridge.apiLevel,
         g_state.bridge.available ? 1 : 0,
         g_state.jvmti.available ? 1 : 0,
         g_state.art.hasArtInternals ? 1 : 0,
         g_state.art.flavor.c_str());
}

bool ArtGuard::is_initialized() const {
    std::lock_guard<std::mutex> lk(g_state.mu);
    return g_state.initialized;
}

const BridgeCapabilities& ArtGuard::bridge() const {
    // Caller is expected to have checked is_initialized; we still
    // return the (empty) default if not, to avoid undefined behavior.
    return g_state.bridge;
}

const JvmtiCapabilities& ArtGuard::jvmti() const {
    return g_state.jvmti;
}

const ArtCapabilities& ArtGuard::art() const {
    return g_state.art;
}

bool ArtGuard::is_bridge_available() const {
    std::lock_guard<std::mutex> lk(g_state.mu);
    return g_state.initialized && g_state.bridge.available && g_state.bridge.hasJava;
}

bool ArtGuard::is_jvmti_available() const {
    std::lock_guard<std::mutex> lk(g_state.mu);
    return g_state.initialized && g_state.jvmti.available;
}

bool ArtGuard::is_art_internals_available() const {
    std::lock_guard<std::mutex> lk(g_state.mu);
    return g_state.initialized && g_state.art.hasArtInternals;
}

bool ArtGuard::meets_api_level(int minimum) const {
    std::lock_guard<std::mutex> lk(g_state.mu);
    if (!g_state.initialized) return false;
    return g_state.bridge.apiLevel >= minimum;
}

} // namespace art
} // namespace yamgg
