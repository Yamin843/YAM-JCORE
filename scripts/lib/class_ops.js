handlers["bridge.class.list"] = function (args) {
    if (!bridgeInfo.available) throw new Error("bridge unavailable");
    var filter = args && args.filter ? String(args.filter) : null;
    var classes = [];
    Java.performNow(function () {
        Java.enumerateLoadedClasses({
            onMatch: function (name) {
                if (!filter || name.indexOf(filter) !== -1) classes.push(name);
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
