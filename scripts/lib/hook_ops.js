handlers["bridge.hook.install"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    if (!args.methodName) throw new Error("methodName required");
    var className = args.className;
    var methodName = args.methodName;
    var overloadTypes = args.overloadTypes || [];
    var outcome = { hookId: 0, ok: false };

    Java.performNow(function () {
        var C = Java.use(className);
        var dispatcher = C[methodName];
        if (!dispatcher) throw new Error("method not found: " + className + "." + methodName);
        var method = pickOverload(dispatcher, overloadTypes);

        var hookId = nextHookId++;
        var entry = {
            hookId: hookId,
            className: className,
            methodName: methodName,
            overloadTypes: overloadTypes,
            method: method,
            originalImpl: method.implementation,
            active: true
        };

        method.implementation = function () {
            var argsArr = Array.prototype.slice.call(arguments);
            var safeArgs = [];
            for (var i = 0; i < argsArr.length; i++) {
                safeArgs.push(toJsonSafe(argsArr[i], 0));
            }
            send({ type: "event", name: "hook.enter",
                   payload: { hookId: hookId, className: className,
                              methodName: methodName, args: safeArgs,
                              threadId: Process.getCurrentThreadId() } });

            var retval = undefined, threw = false, errMsg = null;
            try {
                retval = method.apply(this, arguments);
                return retval;
            } catch (e) {
                threw = true;
                errMsg = safeString(e);
                throw e;
            } finally {
                var safeRet = null;
                if (!threw) {
                    try { safeRet = toJsonSafe(retval, 0); } catch (_) {}
                }
                send({ type: "event", name: "hook.leave",
                       payload: { hookId: hookId, className: className,
                                  methodName: methodName, returnValue: safeRet,
                                  threw: threw, error: errMsg } });
            }
        };

        hooks[hookId] = entry;
        outcome.hookId = hookId;
        outcome.ok = true;
    });
    return outcome;
};

handlers["bridge.hook.uninstall"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || typeof args.hookId !== "number") throw new Error("hookId required");
    var hookId = args.hookId;
    var entry = hooks[hookId];
    if (!entry) return { ok: false, error: "no hook with that id" };
    Java.performNow(function () {
        try {
            entry.method.implementation = entry.originalImpl;
        } catch (e) {
            throw new Error("uninstall failed: " + safeString(e));
        }
    });
    entry.active = false;
    delete hooks[hookId];
    return { ok: true, hookId: hookId };
};

handlers["bridge.hook.list"] = function () {
    var out = [];
    for (var k in hooks) {
        if (!hooks.hasOwnProperty(k)) continue;
        var h = hooks[k];
        if (!h.active) continue;
        out.push({ hookId: h.hookId, className: h.className,
                   methodName: h.methodName, overloadTypes: h.overloadTypes });
    }
    return { hooks: out };
};
