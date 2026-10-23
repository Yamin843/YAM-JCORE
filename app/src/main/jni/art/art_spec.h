// art/art_spec.h — ArtMethod / ArtThread / ArtClass / Runtime /
// ClassLinker offset lookup.
//
// Values are fetched on demand from the JS bootstrap agent, which reads
// them from the frida-java-bridge internals that LIBYAMJS embeds. The
// values are cached after first fetch. On API levels the bridge has
// never been tested against, the JS agent returns the nearest known
// value and we log a warning so the caller knows.
#ifndef YAMGG_ART_ART_SPEC_H
#define YAMGG_ART_ART_SPEC_H

#include <cstdint>
#include <string>

namespace yamgg {
namespace art {

struct ArtMethodSpec {
    int  api             = 0;
    int  size            = 0;
    int  jniCode         = 0;
    int  quickCode       = 0;
    int  accessFlags     = 0;
    int  interpreterCode = -1;   // -1 if the field is absent on this API
};

struct ArtThreadSpec {
    int  api                              = 0;
    int  self                             = 0;
    int  exception                        = 0;
    int  throwLocation                    = 0;
    int  topHandleScope                   = 0;
    int  managedStack                     = 0;
    int  isExceptionReportedToInstrumentation = -1;
};

struct ArtClassSpec {
    int  api                   = 0;
    int  ifields               = 0;
    int  methods               = 0;
    int  sfields               = 0;
    int  copiedMethodsOffset   = 0;
};

struct ArtRuntimeSpec {
    int  api          = 0;
    int  heap         = 0;
    int  threadList   = 0;
    int  internTable  = 0;
    int  classLinker  = 0;
    int  jniIdManager = 0;     // 0 if absent
};

struct ClassLinkerSpec {
    int  api                              = 0;
    int  quickResolutionTrampoline        = 0;
    int  quickImtConflictTrampoline       = 0;
    int  quickGenericJniTrampoline        = 0;
    int  quickToInterpreterBridgeTrampoline = 0;
};

class ArtSpec {
public:
    static ArtSpec& instance();

    // Each accessor blocks until the first fetch for that particular
    // spec has returned. Repeated calls use the cached value.
    //
    // Returns false and fills `error` if the fetch failed. Returns true
    // and fills `out` on success.
    bool method(ArtMethodSpec& out, std::string& error, int timeout_ms = 3000);
    bool thread(ArtThreadSpec& out, std::string& error, int timeout_ms = 3000);
    bool klass(ArtClassSpec& out, std::string& error, int timeout_ms = 3000);
    bool runtime(ArtRuntimeSpec& out, std::string& error, int timeout_ms = 3000);
    bool class_linker(ClassLinkerSpec& out, std::string& error, int timeout_ms = 3000);

    // Clears caches so the next call re-fetches. Used when the
    // session restarts.
    void invalidate();

private:
    ArtSpec() = default;
};

} // namespace art
} // namespace yamgg

#endif // YAMGG_ART_ART_SPEC_H
