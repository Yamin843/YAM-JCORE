//
// Java.retain() creates a global ref. The wrapper is kept alive by JS GC,
// which is non-deterministic. For a long-running ImGui tool that creates
// and releases many instances, we keep our own map from handle-string
// to wrapper, and release via env.deleteGlobalRef explicitly.

var retained = {};        // handleString → { wrapper, className, ticks }
var retainedTick = 0;

function retainObject(instance, className) {
    var wrapper = Java.retain(instance);
    var handle = wrapper.$h.toString();
    retained[handle] = { wrapper: wrapper, className: className, ticks: ++retainedTick };
    return handle;
}

function releaseObject(handle) {
    var entry = retained[handle];
    if (!entry) return false;
    delete retained[handle];
    Java.performNow(function () {
        try {
            var env = Java.vm.getEnv();
            env.deleteGlobalRef(entry.wrapper.$h);
        } catch (_) {}
    });
    return true;
}

function releaseAllObjects() {
    var n = 0;
    var keys = Object.keys(retained);
    for (var i = 0; i < keys.length; i++) {
        if (releaseObject(keys[i])) n++;
    }
    return n;
}

function castHandle(handle, className) {
    var p = ptr(handle);
    return Java.cast(p, Java.use(className));
}
