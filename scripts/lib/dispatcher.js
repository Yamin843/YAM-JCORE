recv("cmd", function (message) {
    if (!message || typeof message !== "object") return;
    var id = message.id;
    var op = message.op;
    var args = message.args || {};
    if (typeof id !== "number") return;

    function reply(ok, resultOrError) {
        if (ok) {
            send({ type: "reply", id: id, ok: true,
                   result: toJsonSafe(resultOrError, 0) });
        } else {
            send({ type: "reply", id: id, ok: false,
                   error: safeString(resultOrError) });
        }
    }

    try {
        var handler = handlers[op];
        if (!handler) throw new Error("unknown op: " + op);
        var result = handler(args);
        reply(true, result);
    } catch (e) {
        reply(false, errorMessage(e));
    }
});

var caps = {
    bridge: bridgeInfo,
    jvmti: { available: false, canTagObjects: false },
    flavor: "unknown",
    hasArtInternals: false
};

if (bridgeInfo.available) {
    try {
        Java.performNow(function () {
            var api = Java.api;
            if (!api) return;
            caps.flavor = api.flavor || "unknown";
            if (api.jvmti) {
                caps.jvmti.available = true;
                try {
                    var capsBuf = Memory.alloc(8);
                    capsBuf.writeU64(0);
                    var env = api.jvmti;
                    var fn = env.vtable.add(140 * Process.pointerSize).readPointer();
                    var getCaps = new NativeFunction(fn, "int",
                        ["pointer", "pointer"], { exceptions: "propagate" });
                    if (getCaps(env.handle, capsBuf) === 0) {
                        caps.jvmti.canTagObjects =
                            ((capsBuf.readU64().valueOf() & 1) !== 0);
                    }
                } catch (_) {}
            }
            caps.hasArtInternals = !!(api.artRuntime && api.artClassLinker);
        });
    } catch (e) {
        caps.probeError = safeString(e);
    }
}

send({ type: "event", name: "capabilities", payload: caps });

send({
    type: "ready",
    phase: "bootstrap",
    hasJava: bridgeInfo.hasJava,
    bridge: {
        variant:   bridgeInfo.variant,
        available: bridgeInfo.available,
        version:   bridgeInfo.apiLevel
    }
});
