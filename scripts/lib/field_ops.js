handlers["bridge.field.read"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    if (!args.fieldName) throw new Error("fieldName required");
    var outcome = { ok: false, value: null, typeName: null };
    Java.performNow(function () {
        var C = Java.use(args.className);
        var isStatic = !args.thisHandle;
        var field = C[args.fieldName];
        if (!field) throw new Error("field not found");
        try {
            outcome.typeName = field.fieldType ? field.fieldType.className : null;
        } catch (_) {}

        var raw;
        if (isStatic) {
            raw = field.value;
        } else {
            var instance = castHandle(args.thisHandle, args.className);
            raw = field.value.call(instance);
        }
        outcome.value = toJsonSafe(raw, 0);
        outcome.ok = true;
    });
    return outcome;
};

handlers["bridge.field.write"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    if (!args.fieldName) throw new Error("fieldName required");
    if (!("value" in args)) throw new Error("value required");
    var outcome = { ok: false, typeName: null };
    Java.performNow(function () {
        var C = Java.use(args.className);
        var field = C[args.fieldName];
        if (!field) throw new Error("field not found");
        var fieldType = field.fieldType ? field.fieldType.className : "java.lang.Object";
        outcome.typeName = fieldType;
        var env = Java.vm.getEnv();
        var value = fromJsonForJava(args.value, fieldType, env);
        if (!args.thisHandle) {
            field.value = value;
        } else {
            var instance = castHandle(args.thisHandle, args.className);
            field.value.call(instance, value);
        }
        outcome.ok = true;
    });
    return outcome;
};
