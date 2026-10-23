// Convert a Java handle (previously retained via bridge.call.create or
// bridge.field.read returning a JavaObject) into a rich JSON
// representation using toJsonSafe. Returns the resulting JSON.
handlers["bridge.convert.java_to_json"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.handle) throw new Error("handle required");
    if (!args.className) throw new Error("className required");
    var handle = args.handle;
    var className = args.className;
    var json = null;
    Java.performNow(function () {
        var env = Java.vm.getEnv();
        var instance = Java.cast(ptr(handle), Java.use(className));
        json = toJsonSafe(instance, 0);
    });
    return { value: json };
};

// Convert a JSON value into a Java value of the given class name.
// The result is retained and returned as a handle.
handlers["bridge.convert.json_to_java"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    var className = args.className;
    var value = args.value;
    var out = { handle: null, className: className };
    Java.performNow(function () {
        var env = Java.vm.getEnv();
        var javaValue = fromJsonForJava(value, className, env);
        if (javaValue === null || javaValue === undefined) {
            out.handle = null;
            return;
        }
        // Primitive results have no handle; wrap them in a boxed type so
        // the UI can carry them around.
        if (typeof javaValue === "string" ||
            typeof javaValue === "number" ||
            typeof javaValue === "boolean") {
            out.handle = null;
            out.literal = javaValue;
            out.literalKind = typeof javaValue;
            return;
        }
        if (javaValue && javaValue.$h) {
            var retained = Java.retain(javaValue);
            out.handle = retained.$h.toString();
        }
    });
    return out;
};

// Convert a JSON value into the Java representation of a given type,
// then back into JSON. Used by the UI to preview conversions.
handlers["bridge.convert.roundtrip"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    var className = args.className;
    var value = args.value;
    var out = { converted: null };
    Java.performNow(function () {
        var env = Java.vm.getEnv();
        var javaValue = fromJsonForJava(value, className, env);
        out.converted = toJsonSafe(javaValue, 0);
    });
    return out;
};
