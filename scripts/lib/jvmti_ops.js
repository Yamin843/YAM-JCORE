handlers["jvmti.add_capabilities"] = function () {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var result = { added: false, canTagObjects: false };
    Java.performNow(function () {
        var api = Java.api;
        if (!api || !api.jvmti) return;
        try {
            var caps = Memory.alloc(8);
            caps.writeU64(1);
            var env = api.jvmti;
            var fn = env.vtable.add(141 * Process.pointerSize).readPointer();
            var addCaps = new NativeFunction(fn, "int", ["pointer", "pointer"],
                { exceptions: "propagate" });
            var rc = addCaps(env.handle, caps);
            result.added = (rc === 0);
            result.canTagObjects = (rc === 0);
        } catch (e) {
            result.error = safeString(e);
        }
    });
    return result;
};

handlers["jvmti.get_loaded_classes"] = function () {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var out = [];
    Java.performNow(function () {
        Java.enumerateLoadedClasses({
            onMatch: function (name) { out.push(name); },
            onComplete: function () {}
        });
    });
    return { classes: out };
};

handlers["jvmti.get_class_methods"] = function (args) {
    if (!args || !args.className) throw new Error("className required");
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var out = { className: args.className, methods: [], fields: [] };
    Java.performNow(function () {
        var C = Java.use(args.className);
        var members = C.$ownMembers || [];
        for (var i = 0; i < members.length; i++) {
            var name = members[i];
            try {
                var v = C[name];
                if (typeof v === "function" && v.overloads) {
                    for (var j = 0; j < v.overloads.length; j++) {
                        var ov = v.overloads[j];
                        var argList = [];
                        for (var k = 0; k < ov.argumentTypes.length; k++) {
                            argList.push(ov.argumentTypes[k].className);
                        }
                        out.methods.push({
                            name: name,
                            returnType: ov.returnType ? ov.returnType.className : "void",
                            argumentTypes: argList,
                            type: ov.type
                        });
                    }
                } else if (v && typeof v === "object" && "value" in v) {
                    out.fields.push({
                        name: name,
                        type: v.fieldType ? v.fieldType.className : null
                    });
                }
            } catch (_) {}
        }
    });
    return out;
};

// ─── bridge.deoptimize.* ────────────────────────────────────────
//
// Deoptimization forces ART to abandon optimized (JIT/AOT) code for
// the affected methods and fall back to the interpreter. This is
// CRITICAL for reliable hooking on modern Android: when a method has
// been AOT-compiled, its callers may have inlined copies that our
// Interceptor cannot see. Deoptimizing first makes .implementation
// stable.
//
// The bridge exposes three deopt levels:
//   deoptimizeEverything()   — the whole runtime (slow but safest)
//   deoptimizeBootImage()    — only boot image classes
//   deoptimizeMethod(m)      — a specific ArtMethod
handlers["bridge.deoptimize.everything"] = function () {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (bridgeInfo.apiLevel < 24) {
        throw new Error("deoptimization requires API 24+");
    }
    Java.performNow(function () {
        Java.deoptimizeEverything();
    });
    return { ok: true, scope: "everything" };
};

handlers["bridge.deoptimize.boot_image"] = function () {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (bridgeInfo.apiLevel < 26) {
        throw new Error("deoptimizeBootImage requires API 26+");
    }
    Java.performNow(function () {
        Java.deoptimizeBootImage();
    });
    return { ok: true, scope: "boot_image" };
};

// ─── bridge.deoptimize.method ───────────────────────────────────
// Deoptimizes a single method. Pass className + methodName +
// overloadTypes (optional). This is less disruptive than
// deoptimizeEverything.
handlers["bridge.deoptimize.method"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    if (!args.methodName) throw new Error("methodName required");
    if (bridgeInfo.apiLevel < 24) {
        throw new Error("deoptimizeMethod requires API 24+");
    }

    var className     = args.className;
    var methodName    = args.methodName;
    var overloadTypes = args.overloadTypes || [];

    var result = { ok: false };
    Java.performNow(function () {
        var C = Java.use(className);
        var dispatcher = C[methodName];
        if (!dispatcher) throw new Error("method not found: " + className + "." + methodName);
        var method = dispatcher;
        if (overloadTypes.length > 0) {
            method = dispatcher.overload.apply(dispatcher, overloadTypes);
        }
        Java.deoptimizeMethod(method);
        result.ok = true;
    });

    return result;
};

