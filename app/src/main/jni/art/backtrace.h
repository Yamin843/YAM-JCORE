// art/backtrace.h — Java stack trace capture.
//
// The bridge's Java.backtrace() walks the JS thread's ART stack. It
// returns a Backtrace object whose .frames array carries signature,
// origin, className, methodName, methodFlags, fileName, and lineNumber
// for each frame — the same data the bridge's ArtStackVisitor produces.
//
// Cross-thread backtraces are not available through Java.backtrace
// because it uses StackVisitor on the current thread only. The bridge
// has the primitives to do cross-thread but does not expose them; the
// capture_thread method below reports the limitation with a clear error.
#ifndef YAMGG_ART_BACKTRACE_H
#define YAMGG_ART_BACKTRACE_H

#include <cstdint>
#include <string>
#include <vector>

namespace yamgg {
namespace art {

struct Frame {
    std::string signature;
    std::string className;
    std::string methodName;
    std::string origin;
    std::string fileName;
    int         lineNumber  = -1;
    uint32_t    methodFlags = 0;
};

struct BacktraceResult {
    std::vector<Frame> frames;
};

class Backtrace {
public:
    static Backtrace& instance();

    // Captures the Java stack of the JS thread. `limit` caps the number
    // of frames (0 = default). Call from worker thread; blocking.
    bool capture(BacktraceResult& out,
                 std::string& error,
                 int limit = 32,
                 int timeout_ms = 5000);

    // Attempts to capture a different thread. Currently unsupported by
    // the bridge's public API; returns false with an explanatory error.
    bool capture_thread(uint32_t threadId,
                        BacktraceResult& out,
                        std::string& error,
                        int limit = 32,
                        int timeout_ms = 5000);

private:
    Backtrace() = default;
};

} // namespace art
} // namespace yamgg

#endif // YAMGG_ART_BACKTRACE_H
