handlers["bridge.handle.release"] = function (args) {
    if (!args || !args.handle) throw new Error("handle required");
    var released = releaseObject(args.handle);
    return { ok: released, handle: args.handle };
};

handlers["bridge.handle.releaseAll"] = function () {
    var count = releaseAllObjects();
    return { ok: true, released: count };
};

handlers["bridge.handle.count"] = function () {
    return { count: Object.keys(retained).length };
};
