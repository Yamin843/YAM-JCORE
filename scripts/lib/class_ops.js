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

// ─── bridge.class.enumerate_methods ─────────────────────────────
//
// Java.enumerateMethods is a bridge API that uses JVMTI or ART
// VisitClasses to find methods matching a pattern, without loading
// every class. Pattern format: "className!methodName"
//   "*!*"                    → every method in every class
//   "com.example.*!*"        → every method in com.example.*
//   "*!onCreate"             → every onCreate method
//   "com.example.Foo!bar"    → exact match
//
// Returns matches grouped by class loader.
handlers["bridge.class.enumerate_methods"] = function (args) {
    if (!args || !args.pattern) throw new Error("pattern required");
    if (!bridgeInfo.available) throw new Error("bridge unavailable");

    var pattern = String(args.pattern);
    var include_signature = args.includeSignature ? true : false;
    var ignore_case       = args.ignoreCase ? true : false;
    var skip_system       = args.skipSystemClasses ? true : false;

    var matches = [];
    Java.performNow(function () {
        var results = Java.enumerateMethods(
            pattern
            + (include_signature ? "/s" : "")
            + (ignore_case       ? "/i" : "")
            + (skip_system       ? "/u" : ""));
        for (var i = 0; i < results.length; i++) {
            var group = results[i];
            var loaderHandle = null;
            if (group.loader) {
                try { loaderHandle = Java.retain(group.loader).$h.toString(); } catch (e) {}
            }
            for (var j = 0; j < group.classes.length; j++) {
                var cls = group.classes[j];
                for (var k = 0; k < cls.methods.length; k++) {
                    matches.push({
                        className:   cls.name,
                        methodName:  cls.methods[k],
                        loader:      loaderHandle,
                        loaderDesc:  group.loader ? safeString(group.loader) : "<boot>"
                    });
                }
            }
        }
    });

    return { matches: matches, pattern: pattern };
};

// ─── bridge.classloaders.list ───────────────────────────────────
//
// Lists every class loader the runtime knows about. Useful for
// finding classes that were loaded by a non-system loader (games,
// dynamic feature modules, plugins).
handlers["bridge.classloaders.list"] = function () {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var loaders = [];
    Java.performNow(function () {
        Java.enumerateClassLoaders({
            onMatch: function (loader) {
                var handle = null;
                try { handle = Java.retain(loader).$h.toString(); } catch (e) {}
                loaders.push({
                    handle: handle,
                    description: safeString(loader)
                });
            },
            onComplete: function () {}
        });
    });
    return { loaders: loaders };
};
