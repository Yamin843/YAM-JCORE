handlers["heap.choose"] = function (args) {
    if (!args || !args.className) throw new Error("className required");
    if (typeof args.sessionId !== "number") throw new Error("sessionId required");
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var className = args.className;
    var sessionId = args.sessionId;
    Java.performNow(function () {
        Java.choose(className, {
            onMatch: function (instance) {
                var handle = null;
                try { handle = retainObject(instance, className); } catch (_) {}
                send({ type: "event", name: "heap.match",
                       payload: { sessionId: sessionId, className: className,
                                  handle: handle, toString: safeString(instance) } });
            },
            onComplete: function () {
                send({ type: "event", name: "heap.complete",
                       payload: { sessionId: sessionId, className: className } });
            }
        });
    });
    return { started: true, sessionId: sessionId };
};

handlers["heap.visit_objects"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (typeof args.sessionId !== "number") throw new Error("sessionId required");
    var sessionId = args.sessionId;

    // Uses ART's VisitObjects directly through the API the bridge exposes.
    // This is the same approach Java.choose uses internally on API < 30,
    // and is dramatically faster than Java.choose per class.
    Java.performNow(function () {
        var api = Java.api;
        if (!api || !api["art::gc::Heap::VisitObjects"]) {
            send({ type: "event", name: "heap.visit_complete",
                   payload: { sessionId: sessionId,
                              error: "VisitObjects unavailable" } });
            return;
        }
        var visitor = api.makeObjectVisitorPredicate(function (objPtr) {
            try {
                var handle = null;
                var env = Java.vm.getEnv();
                var globalRef = api["art::JavaVMExt::AddGlobalRef"](
                    api.vm, Java.vm.getEnv().handle, objPtr);
                if (globalRef && !globalRef.isNull()) {
                    var className = null;
                    try {
                        className = Java.vm.getEnv().getObjectClassName(globalRef);
                    } catch (_) {}
                    var wrapper = Java.cast(globalRef, Java.use(
                        className || "java.lang.Object"));
                    var stored = Java.retain(wrapper);
                    handle = stored.$h.toString();
                    retained[handle] = { wrapper: stored, className: className,
                                          ticks: ++retainedTick };
                }
                send({ type: "event", name: "heap.match",
                       payload: { sessionId: sessionId,
                                  className: className, handle: handle,
                                  toString: className ? ("<" + className + ">") : "<object>" } });
            } catch (_) {}
            return false;  // never stop on match
        });
        try {
            api["art::gc::Heap::VisitObjects"](api.artHeap, visitor, NULL);
        } catch (e) {
            send({ type: "event", name: "heap.visit_complete",
                   payload: { sessionId: sessionId, error: safeString(e) } });
            return;
        }
        send({ type: "event", name: "heap.visit_complete",
               payload: { sessionId: sessionId } });
    });
    return { started: true, sessionId: sessionId };
};
