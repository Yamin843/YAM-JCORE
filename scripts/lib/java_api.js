
// ═══════════════════════════════════════════════════════════════════
//  Java bridge APIs — completion layer
// ═══════════════════════════════════════════════════════════════════
//
// Wraps the remaining public APIs exposed by frida-java-bridge that
// were not yet surfaced to the C++ side. Each handler is a thin shim:
// it does no processing beyond validating arguments and returning a
// JSON representation of the result.

// ─── bridge.java.synchronized ───────────────────────────────────
// Executes `body` (a JS source string) inside Java.synchronized(obj).
// The user passes `handle` (a retained object handle) and `bodyCode`.
handlers["bridge.java.synchronized"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.handle) throw new Error("handle required");
    if (!args.bodyCode) throw new Error("bodyCode required");

    var handle = args.handle;
    var bodyFn = new Function("return (" + args.bodyCode + ")")();
    var result;

    Java.performNow(function () {
        var obj = castHandle(handle, args.className || "java.lang.Object");
        var out;
        Java.synchronized(obj, function () {
            out = bodyFn.call(obj);
        });
        result = toJsonSafe(out, 0);
    });

    return { ok: true, result: result };
};

// ─── bridge.java.is_main_thread ─────────────────────────────────
handlers["bridge.java.is_main_thread"] = function () {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var isMain = false;
    Java.performNow(function () {
        isMain = Java.isMainThread();
    });
    return { isMainThread: isMain };
};

// ─── bridge.java.schedule_on_main ───────────────────────────────
// Schedules `bodyCode` on the Android main thread. Useful for
// operations that must run on the UI thread (e.g. creating a View,
// showing a Toast). Does not wait for completion; use events to
// receive results.
handlers["bridge.java.schedule_on_main"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.bodyCode) throw new Error("bodyCode required");
    if (typeof args.callbackId !== "number") throw new Error("callbackId required");

    var bodyFn = new Function("return (" + args.bodyCode + ")")();
    var callbackId = args.callbackId;

    Java.performNow(function () {
        Java.scheduleOnMainThread(function () {
            try {
                var result = bodyFn.call(null);
                send({ type: "event", name: "java.main_result",
                       payload: { callbackId: callbackId, ok: true,
                                  result: toJsonSafe(result, 0) } });
            } catch (e) {
                send({ type: "event", name: "java.main_result",
                       payload: { callbackId: callbackId, ok: false,
                                  error: safeString(e) } });
            }
        });
    });

    return { scheduled: true, callbackId: callbackId };
};

// ─── bridge.java.array ──────────────────────────────────────────
// Creates a Java array of the given type from JSON elements.
//   type: "int", "byte", "java.lang.String", "com.example.Foo" etc.
handlers["bridge.java.array"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.type) throw new Error("type required");
    if (!Array.isArray(args.elements)) throw new Error("elements must be array");

    var typeName = args.type;
    var elements = args.elements;

    // Map primitives to the JNI short names frida-java-bridge expects.
    var primitiveMap = {
        "boolean": "boolean", "byte": "byte", "char": "char",
        "short":   "short",   "int":  "int",  "long": "long",
        "float":   "float",   "double": "double"
    };

    var arrayType;
    if (primitiveMap[typeName]) arrayType = primitiveMap[typeName];
    else if (typeName[0] === "[") arrayType = typeName;   // already an array descriptor
    else arrayType = "java.lang.Object";   // will be cast per-element

    var result;
    Java.performNow(function () {
        // Convert each element.
        var converted = [];
        for (var i = 0; i < elements.length; i++) {
            if (primitiveMap[typeName]) {
                converted.push(elements[i]);
            } else {
                converted.push(fromJsonForJava(elements[i], typeName, Java.vm.getEnv()));
            }
        }
        var arr = Java.array(arrayType, converted);
        result = retainObject(arr, "[" + typeName);
    });

    return { ok: true, handle: result };
};

// ─── bridge.java.open_class_file ────────────────────────────────
// Loads a DEX file at runtime through Java.openClassFile and returns
// a handle to the DexFile. Use bridge.java.classloader_from_dexfile
// to build a ClassFactory for it.
handlers["bridge.java.open_class_file"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.path) throw new Error("path required");

    var result;
    Java.performNow(function () {
        var dexFile = Java.openClassFile(args.path);
        if (dexFile) {
            dexFile.load();
            result = retainObject(dexFile, "dalvik.system.DexFile");
        }
    });

    if (!result) throw new Error("openClassFile returned null");
    return { ok: true, handle: result };
};

// ─── bridge.java.new_class_factory ──────────────────────────────
// Creates an isolated ClassFactory bound to a specific class loader.
// Useful for finding classes in apps with multiple loaders.
handlers["bridge.java.new_class_factory"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var result = { factoryId: g_next_factory_id++ };
    var handle = args && args.loaderHandle ? args.loaderHandle : null;

    Java.performNow(function () {
        var factory = new Java.ClassFactory();
        if (handle) {
            var loader = castHandle(handle, "java.lang.ClassLoader");
            factory.loader = loader;
        }
        g_factories[result.factoryId] = factory;
    });

    return result;
};

var g_factories = {};
var g_next_factory_id = 1;

// ─── bridge.java.factory_use ────────────────────────────────────
// Uses a class through a specific factory.
handlers["bridge.java.factory_use"] = function (args) {
    if (!args || typeof args.factoryId !== "number") throw new Error("factoryId required");
    if (!args.className) throw new Error("className required");

    var factory = g_factories[args.factoryId];
    if (!factory) throw new Error("factory " + args.factoryId + " not found");

    var result = { ok: false, className: args.className, methods: [], fields: [] };
    Java.performNow(function () {
        var C = factory.use(args.className);
        for (var i = 0; i < C.$ownMembers.length; i++) {
            var name = C.$ownMembers[i];
            try {
                var v = C[name];
                if (typeof v === "function" && v.overloads) {
                    for (var j = 0; j < v.overloads.length; j++) {
                        var ov = v.overloads[j];
                        result.methods.push({
                            name: name,
                            returnType: ov.returnType ? ov.returnType.className : "void",
                            argumentTypes: ov.argumentTypes.map(function (t) { return t.className; }),
                            type: ov.type
                        });
                    }
                } else if (v && typeof v === "object" && "value" in v) {
                    result.fields.push({ name: name, type: v.fieldType ? v.fieldType.className : null });
                }
            } catch (_) {}
        }
        result.ok = true;
    });

    return result;
};

// ─── bridge.java.vm_info ────────────────────────────────────────
handlers["bridge.java.vm_info"] = function () {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var info = { available: true };
    Java.performNow(function () {
        try {
            var vm = Java.vm;
            info.hasVm = !!vm;
            if (vm) {
                info.apiLevel = Java.androidVersion;
                info.isMainThread = Java.isMainThread();
                try { info.currentThreadId = Process.getCurrentThreadId(); } catch (_) {}
                try {
                    var env = vm.getEnv();
                    info.envHandle = env ? env.handle.toString() : null;
                } catch (_) {}
            }
        } catch (e) {
            info.error = safeString(e);
        }
    });
    return info;
};

// ─── bridge.java.perform_deferred ───────────────────────────────
// Uses Java.perform (not performNow). Needed before the app's
// Application has finished initializing, or when interacting with
// boot image classes.
handlers["bridge.java.perform_deferred"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.bodyCode) throw new Error("bodyCode required");
    if (typeof args.callbackId !== "number") throw new Error("callbackId required");

    var bodyFn = new Function("return (" + args.bodyCode + ")")();
    var callbackId = args.callbackId;

    Java.perform(function () {
        try {
            var result = bodyFn.call(null);
            send({ type: "event", name: "java.perform_result",
                   payload: { callbackId: callbackId, ok: true,
                              result: toJsonSafe(result, 0) } });
        } catch (e) {
            send({ type: "event", name: "java.perform_result",
                   payload: { callbackId: callbackId, ok: false,
                              error: safeString(e) } });
        }
    });

    return { scheduled: true, callbackId: callbackId };
};

