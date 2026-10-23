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
    if (!args || typeof args.threadId !== "number") {
        throw new Error("threadId required");
    }
    var threadId = args.threadId;
    var limit = args.limit || 64;

    // Java.backtrace() only walks the current thread. For an arbitrary
    // thread, the bridge does not expose a Java-stack walker. Instead,
    // we return a **native** stack trace of the specified thread via
    // Thread.backtrace, which is what the Java stack ultimately rests
    // on. Symbolized frames are returned as an array of objects.

    var thread = null;
    try {
        thread = Process.findThreadById(threadId);
    } catch (e) {
        thread = null;
    }
    if (thread === null) {
        throw new Error("thread " + threadId + " not found");
    }

    var ctx = null;
    try {
        ctx = thread.context;
    } catch (e) {
        ctx = null;
    }

    var addrs = [];
    try {
        addrs = Thread.backtrace(ctx, Backtracer.ACCURATE).slice(0, limit);
    } catch (e) {
        throw new Error("backtrace failed: " + (e && e.message));
    }

    var frames = [];
    for (var i = 0; i < addrs.length; i++) {
        var sym = null;
        try { sym = DebugSymbol.fromAddress(addrs[i]); } catch (e) {}
        var frame = {
            index: i,
            address: addrs[i].toString(),
            module:   sym && sym.moduleName ? sym.moduleName : null,
            symbol:   sym && sym.name       ? sym.name       : null,
            offset:   sym && sym.name && sym.moduleName
                ? addrs[i].sub(Process.getModuleByName(sym.moduleName).base).toString()
                : null
        };
        frames.push(frame);
    }

    return {
        frames: frames,
        kind: "native",
        note: "Thread.backtrace; Java stack of a non-current thread is not exposed by the bridge"
    };
};;
