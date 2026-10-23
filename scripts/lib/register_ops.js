
// ═══════════════════════════════════════════════════════════════════
//  bridge.register.class — create a Java class at runtime
// ═══════════════════════════════════════════════════════════════════
//
// Full wrapper around Java.registerClass. The spec is passed as JSON:
//
//   {
//     "name": "com.example.HookedActivity",
//     "superClassName": "android.app.Activity",         // optional
//     "implements": ["java.lang.Runnable"],              // optional
//     "fields": {
//       "counter": { "type": "int" },
//       "label":   { "type": "java.lang.String", "value": "hello" }
//     },
//     "methods": {
//       "run": {
//         "returnType": "void",
//         "argumentTypes": [],
//         "implementation": "function() { ... }"        // JS source
//       },
//       "staticAdd": {
//         "returnType": "int",
//         "argumentTypes": ["int", "int"],
//         "isStatic": true,
//         "implementation": "function(a, b) { return a + b; }"
//       },
//       "nativeHook": {
//         "returnType": "void",
//         "argumentTypes": ["int"],
//         "isNative": true
//       }
//     }
//   }
//
// Returns a retained handle to the created class.
handlers["bridge.register.class"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.spec) throw new Error("spec required");
    if (typeof args.spec !== "object") throw new Error("spec must be object");

    var spec = args.spec;
    if (!spec.name) throw new Error("spec.name required");
    if (typeof spec.name !== "string") throw new Error("spec.name must be string");

    var javaSpec = { name: spec.name };

    if (spec.superClassName) {
        javaSpec.superClass = Java.use(spec.superClassName);
    }

    if (spec.implements && Array.isArray(spec.implements)) {
        javaSpec.implements = spec.implements.map(function (n) {
            return Java.use(n);
        });
    }

    if (spec.fields && typeof spec.fields === "object") {
        javaSpec.fields = {};
        for (var fname in spec.fields) {
            if (!spec.fields.hasOwnProperty(fname)) continue;
            var fdef = spec.fields[fname];
            if (typeof fdef === "string") {
                javaSpec.fields[fname] = fdef;
            } else if (fdef && typeof fdef === "object" && fdef.type) {
                var fclone = { type: fdef.type };
                if ("value" in fdef) fclone.value = fdef.value;
                javaSpec.fields[fname] = fclone;
            }
        }
    }

    if (spec.methods && typeof spec.methods === "object") {
        javaSpec.methods = {};
        for (var mname in spec.methods) {
            if (!spec.methods.hasOwnProperty(mname)) continue;
            var mdef = spec.methods[mname];
            if (!mdef || typeof mdef !== "object") continue;

            var mclone = {
                returnType: mdef.returnType || "void",
                argumentTypes: mdef.argumentTypes || []
            };

            if (mdef.isStatic) mclone.isStatic = true;
            if (mdef.isNative) mclone.isNative = true;

            if (mdef.implementation) {
                if (typeof mdef.implementation === "string") {
                    // Compile user code. The function receives the method's
                    // args as positional parameters and `this` is the instance.
                    try {
                        mclone.implementation = new Function(
                            "return (" + mdef.implementation + ")")();
                    } catch (e) {
                        throw new Error("failed to compile method '" + mname + "': " + safeString(e));
                    }
                } else if (typeof mdef.implementation === "function") {
                    mclone.implementation = mdef.implementation;
                }
            }

            javaSpec.methods[mname] = mclone;
        }
    }

    var newClass = null;
    var error = null;
    Java.performNow(function () {
        try {
            newClass = Java.registerClass(javaSpec);
        } catch (e) {
            error = safeString(e);
        }
    });

    if (error) throw new Error(error);
    if (!newClass) throw new Error("Java.registerClass returned null");

    // Retain a wrapper so the class stays alive.
    var wrapper = Java.retain(newClass.class);
    var handle = wrapper.$h.toString();
    retained[handle] = { wrapper: wrapper, className: spec.name, ticks: ++retainedTick };

    return {
        ok: true,
        className: spec.name,
        handle: handle,
        superClass: spec.superClassName || "java.lang.Object",
        implements: spec.implements || []
    };
};

// ─── bridge.register.class_info ─────────────────────────────────
// Returns details of the previously registered classes.
handlers["bridge.register.class_info"] = function (args) {
    if (!args || !args.handle) throw new Error("handle required");
    var handle = args.handle;
    var entry = retained[handle];
    if (!entry) throw new Error("handle not found");

    var info = { className: entry.className, methods: [], fields: [] };
    Java.performNow(function () {
        var C = Java.cast(ptr(handle), Java.use("java.lang.Class"));
        info.isInterface = C.isInterface();
        info.isArray     = C.isArray();
        info.isPrimitive = C.isPrimitive();
        info.name        = C.getName();
        info.simpleName  = C.getSimpleName();
    });
    return info;
};

