// art/art_guard.h — single source of truth for "what can this device do?"
//
// Populated once by the `capabilities` event emitted from bootstrap.js
// during initialisation. Read-only afterwards. Every other art/* module
// asks the guard before attempting an operation so failures are
// reported clearly instead of surfacing as opaque JNI exceptions.
#ifndef YAMGG_ART_ART_GUARD_H
#define YAMGG_ART_ART_GUARD_H

#include <cstdint>
#include <string>

namespace yamgg {
namespace art {

struct BridgeCapabilities {
    bool         reported       = false;
    bool         available      = false;
    bool         hasJava        = false;
    std::string  variant;                      // "official" | "notarthook" | ""
    int          apiLevel       = 0;
};

struct JvmtiCapabilities {
    bool         available      = false;
    bool         canTagObjects  = false;
};

struct ArtCapabilities {
    std::string  flavor;                       // "art" | "jvm" | "dalvik" | "unknown"
    bool         hasArtInternals = false;      // ArtMethod/ClassLinker accessible
};

class ArtGuard {
public:
    static ArtGuard& instance();

    // Called by bootstrap once when the capabilities event arrives.
    // Subsequent calls overwrite with the newest data (used when a
    // session restart re-emits the event).
    void update(const BridgeCapabilities& bridge,
                const JvmtiCapabilities&  jvmti,
                const ArtCapabilities&    art);

    // Marks the guard as unpopulated — no capabilities have been
    // received yet. Any operation that requires them will fail early.
    bool is_initialized() const;

    // Read-only access to the stored capabilities. Callers use these
    // to short-circuit operations before sending commands to JS.
    const BridgeCapabilities& bridge() const;
    const JvmtiCapabilities&  jvmti() const;
    const ArtCapabilities&    art() const;

    // Convenience predicates.
    bool is_bridge_available() const;
    bool is_jvmti_available() const;
    bool is_art_internals_available() const;

    // Returns true if the bridge exposes at least the API level the
    // caller requires. Zero means "no minimum".
    bool meets_api_level(int minimum) const;

private:
    ArtGuard() = default;
};

} // namespace art
} // namespace yamgg

#endif // YAMGG_ART_ART_GUARD_H
