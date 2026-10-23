// core/error_recovery.h — capability detection and graceful degradation.
//
// The embedded Java bridge exposes several distinct capabilities.
// Depending on the target device's libart build, one or more of them
// may be unavailable. Instead of failing the whole session, we detect
// what is available and downgrade to the strongest mode that works.
//
// Capabilities (from frida-java-bridge internals):
//
//   FULL        ArtMethod mangler works. .implementation swaps are
//               stable, Java.choose is fast, class enumeration via
//               VisitClasses works.
//
//   ART_ONLY    ArtMethod mangler is partially working. Some hooks
//               succeed, others fall back to the interpreter bridge.
//               Java.choose is slow (VisitObjects).
//
//   JVMTI_ONLY  Only JVMTI is usable. enumerateLoadedClasses and
//               enumerateMethods work via JVMTI, but .implementation
//               swaps are limited to methods that have already been
//               deoptimized.
//
//   JNI_ONLY    Only raw JNI. Class enumeration walks the DEX path
//               list manually. No hooking. No instance enumeration.
//
//   DEAD        libart is missing or unusable. Session is useless.
//
// Detection is cheap: we call `__yamgg_bridge_ready__()` once and
// inspect the returned `available` + `version` fields, then we probe
// Java.vm.getEnv() from a short eval to confirm JNI works.
#ifndef YAMGG_CORE_ERROR_RECOVERY_H
#define YAMGG_CORE_ERROR_RECOVERY_H

#include <string>

namespace yamgg {

enum class Capability : int {
    Dead        = 0,
    JniOnly     = 1,
    JvmtiOnly   = 2,
    ArtOnly     = 3,
    Full        = 4,
};

const char* capability_name(Capability c);
Capability  current_capability();
bool        capability_at_least(Capability required);

// Called by bootstrap after the prelude reports ready. Fills the
// capability struct from the bridge info and, if possible, does an
// extra probe via Java.performNow to confirm JNI works.
void        detect_capabilities_after_ready();

// Force a downgrade to a specific capability. Useful when a runtime
// operation (e.g. ArtMethodMangler::mangle) fails and we want to
// prevent further attempts. Returns true if the change was applied.
bool        downgrade_to(Capability target);

// Convenience wrapper used by MessageRouter: routes a JS "error"
// message about a specific operation into a downgrade decision.
void        note_operation_failure(const std::string& op,
                                    const std::string& error);

} // namespace yamgg

#endif // YAMGG_CORE_ERROR_RECOVERY_H
