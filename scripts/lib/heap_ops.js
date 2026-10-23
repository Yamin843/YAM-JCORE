handlers["heap.choose"] = function (args) {
    if (!args || !args.className) throw new Error("className required");
    if (typeof args.sessionId !== "number") throw new Error("sessionId required");
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var className = args.className;
    var sessionId = args.sessionId;
    var startedAt = Date.now();
    var matchCount = 0;
    var PROGRESS_INTERVAL = 500;

    // Emit a progress event every 500 matches so the UI can display
    // "scanned 1500, elapsed 2.3s" instead of hanging silently.
    Java.performNow(function () {
        Java.choose(className, {
            onMatch: function (instance) {
                matchCount++;
                var handle = null;
                try { handle = retainObject(instance, className); } catch (_) {}
                send({ type: "event", name: "heap.match",
                       payload: { sessionId: sessionId, className: className,
                                  handle: handle, toString: safeString(instance) } });
                if (matchCount % PROGRESS_INTERVAL === 0) {
                    send({ type: "event", name: "heap.progress",
                           payload: { sessionId: sessionId,
                                      className: className,
                                      matches: matchCount,
                                      elapsedMs: Date.now() - startedAt } });
                }
            },
            onComplete: function () {
                send({ type: "event", name: "heap.complete",
                       payload: { sessionId: sessionId, className: className,
                                  matches: matchCount,
                                  elapsedMs: Date.now() - startedAt } });
            }
        });
    });
    return { started: true, sessionId: sessionId, className: className };
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

// ═══════════════════════════════════════════════════════════════════
//  heap.choose_jvmti — JVMTI-based instance enumeration
// ═══════════════════════════════════════════════════════════════════
//
// Java.choose is a wrapper around three possible strategies chosen by
// the bridge based on symbol availability. On API 29 the bridge tends
// to pick VisitObjects which walks every heap object in the process.
//
// This handler bypasses Java.choose and calls JVMTI directly:
//
//   1. IterateOverInstancesOfClass(klass, filter=3, cb, user_data)
//      tags every instance of `klass` with a unique jlong.
//   2. GetObjectsWithTags([tag], ...) retrieves the tagged objects.
//   3. Each retrieved object is cast to the class and retained.
//
// The vtable indices are:
//   112 = IterateOverInstancesOfClass
//   114 = GetObjectsWithTags
//
// These are stable across all ART builds since API 26 because JVMTI is
// a standard interface. This is exactly what frida-core does and does
// not require any modification to the embedded bridge.
handlers["heap.choose_jvmti"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    if (typeof args.sessionId !== "number") throw new Error("sessionId required");

    var className = args.className;
    var sessionId = args.sessionId;
    var startedAt = Date.now();
    var matchCount = 0;

    Java.performNow(function () {
        var api = Java.api;
        if (!api || !api.jvmti) {
            throw new Error("JVMTI unavailable on this device");
        }

        var env    = Java.vm.getEnv();
        var jvmti  = api.jvmti;
        var ptrSz  = Process.pointerSize;

        // Get jclass global ref from the class object.
        var C = Java.use(className);
        var klassObj = C.class;
        var jklass   = klassObj.$h;   // already a global ref
        if (!jklass || jklass.isNull()) {
            throw new Error("could not resolve jclass for " + className);
        }

        // Locate vtable entries.
        var iterateFnAddr = jvmti.vtable.add(112 * ptrSz).readPointer();
        var getObjsFnAddr = jvmti.vtable.add(114 * ptrSz).readPointer();

        var iterate = new NativeFunction(iterateFnAddr, "int",
            ["pointer", "pointer", "int", "pointer", "pointer"],
            { exceptions: "propagate" });
        var getObjects = new NativeFunction(getObjsFnAddr, "int",
            ["pointer", "int", "pointer", "pointer", "pointer", "pointer"],
            { exceptions: "propagate" });

        // Unique tag for this session.
        var tag = int64(sessionId);

        // Callback: tag every object JVMTI hands us.
        var callback = new NativeCallback(function (class_tag, size, tag_ptr, user_data) {
            try { tag_ptr.writeS64(tag); } catch (e) {}
            return 0;  // JVMTI_ITERATION_CONTINUE
        }, "int", ["int64", "int64", "pointer", "pointer"]);

        try {
            // Step 1: tag all instances of the class.
            // filter = 3 (JVMTI_HEAP_OBJECT_EITHER)
            var rc = iterate(jvmti.handle, jklass, 3, callback, NULL);
            if (rc !== 0) {
                throw new Error("IterateOverInstancesOfClass returned " + rc);
            }

            // Step 2: retrieve the tagged objects.
            var tagBuf     = Memory.alloc(8);
            tagBuf.writeS64(tag);
            var countBuf   = Memory.alloc(4);
            var objectsBuf = Memory.alloc(ptrSz);

            rc = getObjects(jvmti.handle, 1, tagBuf, countBuf, objectsBuf, NULL);
            if (rc !== 0) {
                throw new Error("GetObjectsWithTags returned " + rc);
            }

            var count      = countBuf.readS32();
            var objectsPtr = objectsBuf.readPointer();

            // Step 3: collect the raw pointers before deallocating the array.
            var pointers = [];
            for (var i = 0; i < count; i++) {
                pointers.push(objectsPtr.add(i * ptrSz).readPointer());
            }

            // Step 4: deallocate the JVMTI-managed array. Individual
            // refs remain valid until deleteLocalRef.
            try { jvmti.deallocate(objectsPtr); } catch (e) {}

            // Step 5: deliver matches.
            for (var j = 0; j < pointers.length; j++) {
                try {
                    var wrapper = Java.cast(pointers[j], C);
                    var handle  = retainObject(wrapper, className);
                    matchCount++;
                    send({ type: "event", name: "heap.match",
                           payload: { sessionId: sessionId,
                                      className: className,
                                      handle: handle,
                                      toString: "<" + className + ">" } });
                    if (matchCount % 500 === 0) {
                        send({ type: "event", name: "heap.progress",
                               payload: { sessionId: sessionId,
                                          className: className,
                                          matches: matchCount,
                                          elapsedMs: Date.now() - startedAt } });
                    }
                } catch (e) {}
            }

            // Step 6: release the local refs.
            for (var k = 0; k < pointers.length; k++) {
                try { env.deleteLocalRef(pointers[k]); } catch (e) {}
            }
        } finally {
            // Let the wrapper be GC-able.
        }

        send({ type: "event", name: "heap.complete",
               payload: { sessionId: sessionId, className: className,
                          matches: matchCount,
                          elapsedMs: Date.now() - startedAt,
                          strategy: "jvmti" } });
    });

    return { started: true, sessionId: sessionId, strategy: "jvmti" };
};
