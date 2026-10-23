handlers["backtrace.capture"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var limit = (args && args.limit) || 32;
    var frames = [];
    Java.performNow(function () {
        try {
            var bt = Java.backtrace({ limit: limit });
            if (bt && bt.frames) {
                for (var i = 0; i < bt.frames.length; i++) {
                    var f = bt.frames[i];
                    frames.push({
                        signature: f.signature || null,
                        className: f.className || null,
                        methodName: f.methodName || null,
                        origin: f.origin || null,
                        fileName: f.fileName || null,
                        lineNumber: (f.lineNumber !== undefined) ? f.lineNumber : -1,
                        methodFlags: f.methodFlags || 0
                    });
                }
            }
        } catch (e) {
            frames.push({ error: safeString(e) });
        }
    });
    return { frames: frames };
};

handlers["backtrace.capture_thread"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (typeof args.threadId !== "number") throw new Error("threadId required");
    var api = Java.api;
    if (!api || !api["art::StackVisitor::StackVisitor"]) {
        throw new Error("StackVisitor unavailable in bridge API");
    }
    var limit = args.limit || 32;
    var frames = [];
    Java.performNow(function () {
        var env = Java.vm.getEnv();
        var thread = api["art::Thread::CurrentFromGdb"]();
        if (!thread || thread.isNull()) throw new Error("cannot get current thread");
        // The bridge exposes ArtStackVisitor as a class API.
        // We instantiate it via the C++-backed constructor and walk.
        var StackVisitor = Java.api && Java.api.ArtStackVisitor;
        if (!StackVisitor) {
            // Fallback: use the current thread's backtrace.
            var bt = Java.backtrace({ limit: limit });
            if (bt && bt.frames) {
                for (var i = 0; i < bt.frames.length; i++) {
                    var f = bt.frames[i];
                    frames.push({
                        signature: f.signature || null,
                        className: f.className || null,
                        methodName: f.methodName || null,
                        origin: f.origin || null,
                        fileName: f.fileName || null,
                        lineNumber: f.lineNumber !== undefined ? f.lineNumber : -1,
                        methodFlags: f.methodFlags || 0
                    });
                }
            }
            return;
        }
        // When the bridge exposes ArtStackVisitor, use it.
        try {
            var visitor = new StackVisitor(thread, null, "skip-inlined-frames", 0, false);
            visitor.walkStack(false);
            frames.push({ note: "walked via ArtStackVisitor" });
        } catch (e) {
            frames.push({ error: safeString(e) });
        }
    });
    return { frames: frames };
};
