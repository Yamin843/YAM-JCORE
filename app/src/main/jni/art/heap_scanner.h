// art/heap_scanner.h — instance enumeration via the bridge's Java.choose.
//
// Returns matches as a stream of events rather than a single large
// reply. This avoids exceeding the frame size limit for apps with
// tens of thousands of instances, and lets the UI update incrementally.
#ifndef YAMGG_ART_HEAP_SCANNER_H
#define YAMGG_ART_HEAP_SCANNER_H

#include <cstdint>
#include <functional>
#include <string>

namespace yamgg {
namespace art {

struct HeapMatch {
    uint64_t    sessionId   = 0;
    std::string className;
    std::string handle;               // Java.retain(instance).$h as string
    std::string stringRep;            // instance.toString() as seen from JS
};

class HeapScanner {
public:
    using MatchCallback    = std::function<void(const HeapMatch&)>;
    using CompleteCallback = std::function<void(uint64_t sessionId, bool aborted)>;

    static HeapScanner& instance();

    // Start a choose operation. Returns the sessionId that will appear
    // in the callbacks, or 0 on immediate failure (error string set).
    //
    // The callbacks fire from the JS thread. Callers that touch the UI
    // MUST marshal to the UI thread via marshal_to_ui() before returning.
    uint64_t choose(const std::string& className,
                    MatchCallback onMatch,
                    CompleteCallback onComplete,
                    std::string& error,
                    int timeout_ms = 3000);

    // Cancel a running choose. The completion callback (if registered)
    // will fire with aborted=true. Returns true if a session was found.
    bool cancel(uint64_t sessionId);

private:
    HeapScanner() = default;
};

} // namespace art
} // namespace yamgg

#endif // YAMGG_ART_HEAP_SCANNER_H
