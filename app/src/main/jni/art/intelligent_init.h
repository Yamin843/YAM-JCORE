// art/intelligent_init.h — orchestrates bridge init with fallbacks.
//
// Two-phase initialization:
//
//   Phase A: ask the JS bootstrap (smart_init) to poll for libart
//            and, once present, initialize the full bridge.
//
//   Phase B: if Phase A fails, try the direct native::JavaBridge
//            (which only needs JNI_GetCreatedJavaVMs, present in
//            every ART build). This gives the tool a degraded but
//            still-functional mode where field reads, field writes,
//            and simple method calls work, even if the full bridge
//            doesn't.
//
// The chosen mode is reported in IntelligentInitState::mode, which
// the UI uses to decide which features to expose.
#ifndef YAMGG_ART_INTELLIGENT_INIT_H
#define YAMGG_ART_INTELLIGENT_INIT_H

#include <jni.h>

#include <string>

namespace yamgg {
namespace art {

enum class IntelligentMode {
    Unknown,    // not attempted yet
    Full,       // full bridge available (Java.* works)
    JniOnly,    // bridge failed; direct JNI fallback works
    Dead        // nothing works
};

struct IntelligentInitState {
    IntelligentMode mode = IntelligentMode::Unknown;
    std::string     reason;
    std::string     libartPath;
    int             apiLevel = 0;
    bool            bridge_available = false;
    bool            native_jni_available = false;
};

// Kick off the JS-side smart_init. Blocks the caller until either the
// bridge succeeds, the native fallback is initialized, or both fail.
//
// Called from bootstrap_start instead of directly relying on
// Java.available. Returns true if ANY mode (Full or JniOnly) is
// usable. Returns false only in Dead mode.
bool intelligent_initialize(JavaVM* vm);

// Query the last state. Thread-safe.
const IntelligentInitState& intelligent_state();

// Convenience: true if at least JNI-only mode works.
bool intelligent_is_usable();

} // namespace art
} // namespace yamgg

#endif // YAMGG_ART_INTELLIGENT_INIT_H
