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
