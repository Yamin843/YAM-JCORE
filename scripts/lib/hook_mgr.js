var hooks = {};           // hookId → { method, className, methodName, overloadTypes, type }
var nextHookId = 1;

function enumerateOverloads(dispatcher) {
    if (!dispatcher || !dispatcher.overloads) return [];
    var out = [];
    for (var i = 0; i < dispatcher.overloads.length; i++) {
        var ov = dispatcher.overloads[i];
        var types = [];
        for (var k = 0; k < ov.argumentTypes.length; k++) {
            types.push(ov.argumentTypes[k].className);
        }
        out.push({
            argumentTypes: types,
            returnType: ov.returnType ? ov.returnType.className : "void",
            type: ov.type
        });
    }
    return out;
}

function pickOverload(dispatcher, overloadTypes) {
    if (!dispatcher) throw new Error("method not found");
    if (!dispatcher.overloads) {
        // Not an overloaded dispatcher; return the method itself.
        return dispatcher;
    }
    if (!overloadTypes || overloadTypes.length === 0) {
        if (dispatcher.overloads.length === 1) return dispatcher.overloads[0];
        var sigs = enumerateOverloads(dispatcher);
        var human = sigs.map(function (s) {
            return "[" + s.argumentTypes.map(function (t) { return '"' + t + '"'; }).join(",") + "]";
        }).join(" | ");
        throw new Error("ambiguous overload, specify one of: " + human);
    }
    var args = overloadTypes.slice();
    return dispatcher.overload.apply(dispatcher, args);
}
