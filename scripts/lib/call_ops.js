handlers["bridge.call.instance"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    if (!args.methodName) throw new Error("methodName required");
    if (!args.thisHandle) throw new Error("thisHandle required");
    var className = args.className;
    var methodName = args.methodName;
    var overloadTypes = args.overloadTypes || [];
    var thisPtr = ptr(args.thisHandle);
    var jsonArgs = args.args || [];
    var outcome = { ok: false, returnValue: null };

    Java.performNow(function () {
        var C = Java.use(className);
        var dispatcher = C[methodName];
        if (!dispatcher) throw new Error("method not found");
        var method = pickOverload(dispatcher, overloadTypes);
        var instance = Java.cast(thisPtr, C);
        var actual = method.argumentTypes;
        var javaArgs = [];
        for (var i = 0; i < actual.length; i++) {
            var v = (i < jsonArgs.length) ? jsonArgs[i] : null;
            javaArgs.push(fromJsonForJava(v, actual[i].className, Java.vm.getEnv()));
        }
        var result = method.call(instance, javaArgs[0], javaArgs[1], javaArgs[2],
                                  javaArgs[3], javaArgs[4], javaArgs[5],
                                  javaArgs[6], javaArgs[7], javaArgs[8],
                                  javaArgs[9], javaArgs[10], javaArgs[11]);
        outcome.returnValue = toJsonSafe(result, 0);
        outcome.ok = true;
    });
    return outcome;
};

handlers["bridge.call.static"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    if (!args.methodName) throw new Error("methodName required");
    var className = args.className;
    var methodName = args.methodName;
    var overloadTypes = args.overloadTypes || [];
    var jsonArgs = args.args || [];
    var outcome = { ok: false, returnValue: null };

    Java.performNow(function () {
        var C = Java.use(className);
        var dispatcher = C[methodName];
        if (!dispatcher) throw new Error("method not found");
        var method = pickOverload(dispatcher, overloadTypes);
        var actual = method.argumentTypes;
        var javaArgs = [];
        for (var i = 0; i < actual.length; i++) {
            var v = (i < jsonArgs.length) ? jsonArgs[i] : null;
            javaArgs.push(fromJsonForJava(v, actual[i].className, Java.vm.getEnv()));
        }
        var result = method(javaArgs[0], javaArgs[1], javaArgs[2],
                            javaArgs[3], javaArgs[4], javaArgs[5],
                            javaArgs[6], javaArgs[7], javaArgs[8],
                            javaArgs[9], javaArgs[10], javaArgs[11]);
        outcome.returnValue = toJsonSafe(result, 0);
        outcome.ok = true;
    });
    return outcome;
};

handlers["bridge.call.create"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    var className = args.className;
    var ctorTypes = args.ctorTypes || [];
    var jsonArgs = args.args || [];
    var outcome = { ok: false, handle: null, className: className };

    Java.performNow(function () {
        var C = Java.use(className);
        var ctor = pickOverload(C.$init, ctorTypes);
        var actual = ctor.argumentTypes;
        var javaArgs = [];
        for (var i = 0; i < actual.length; i++) {
            var v = (i < jsonArgs.length) ? jsonArgs[i] : null;
            javaArgs.push(fromJsonForJava(v, actual[i].className, Java.vm.getEnv()));
        }
        var instance = ctor.call(C, javaArgs[0], javaArgs[1], javaArgs[2],
                                  javaArgs[3], javaArgs[4], javaArgs[5],
                                  javaArgs[6], javaArgs[7], javaArgs[8],
                                  javaArgs[9], javaArgs[10], javaArgs[11]);
        outcome.handle = retainObject(instance, className);
        outcome.ok = true;
    });
    return outcome;
};
