// Advanced filter matcher. Supports three modes:
//
//   substring        "Bundle"          → name.indexOf("Bundle") !== -1
//   wildcard         "*Bundle*"        → regex with * and ? escaped
//   regex            "/^android\./i"  → full regex
//
// The mode is chosen from the input:
//   - starts with '/' and has a closing '/' → regex (with flags after)
//   - contains '*' or '?'                   → wildcard
//   - otherwise                             → substring
function compileFilter(filter) {
    if (!filter) return null;

    // Regex mode.
    if (filter.length >= 3 && filter[0] === '/') {
        var lastSlash = filter.lastIndexOf('/');
        if (lastSlash > 0) {
            var pattern = filter.substring(1, lastSlash);
            var flags   = filter.substring(lastSlash + 1);
            try {
                var re = new RegExp(pattern, flags);
                return function (name) { return re.test(name); };
            } catch (e) {
                // Invalid regex; fall back to substring.
                return function (name) { return name.indexOf(filter) !== -1; };
            }
        }
    }

    // Wildcard mode.
    if (filter.indexOf('*') !== -1 || filter.indexOf('?') !== -1) {
        var escaped = filter.replace(/[.+^${}()|[\]\\]/g, '\\$&');
        var pattern = escaped.replace(/\*/g, '.*').replace(/\?/g, '.');
        try {
            var re2 = new RegExp('^' + pattern + '$');
            return function (name) { return re2.test(name); };
        } catch (e) {
            return function (name) { return name.indexOf(filter) !== -1; };
        }
    }

    // Substring mode.
    var needle = filter;
    return function (name) { return name.indexOf(needle) !== -1; };
}

handlers["bridge.class.list"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var filter = args && args.filter ? String(args.filter) : null;
    var matcher = compileFilter(filter);
    var classes = [];
    Java.performNow(function () {
        Java.enumerateLoadedClasses({
            onMatch: function (name) {
                if (!matcher || matcher(name)) classes.push(name);
            },
            onComplete: function () {}
        });
    });
    classes.sort();
    return { classes: classes };
};

handlers["bridge.class.inspect"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    if (!args || !args.className) throw new Error("className required");
    var className = args.className;
    var out = { className: className, methods: [], fields: [], constructors: [] };
    Java.performNow(function () {
        var C = Java.use(className);
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
                        if (ov.type === "constructor" || name === "$init") {
                            out.constructors.push({ argumentTypes: argList });
                        } else {
                            out.methods.push({
                                name: name,
                                returnType: ov.returnType ? ov.returnType.className : "void",
                                argumentTypes: argList,
                                type: ov.type
                            });
                        }
                    }
                } else if (v && typeof v === "object" && "value" in v) {
                    var ftype = null;
                    try { ftype = v.fieldType ? v.fieldType.className : null; } catch (_) {}
                    out.fields.push({ name: name, type: ftype });
                }
            } catch (_) {}
        }
    });
    return out;
};
