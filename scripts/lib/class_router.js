
// ═══════════════════════════════════════════════════════════════════
//  class_router.js — resolve a class through any available loader
// ═══════════════════════════════════════════════════════════════════
//
// The default Java.use(className) tries only the boot classpath. When
// an app has plugin-style classes loaded by a custom ClassLoader, the
// default lookup fails. This handler tries each loader in turn and
// returns the one that succeeds, together with the loader handle so
// the UI can bind subsequent operations to the same scope.

var g_class_loader_cache = {};   // className → { handle, description }

handlers["class.resolve"] = function (args) {
    if (!args || !args.className) throw new Error("className required");
    if (!globalThis.Java || !Java.available) {
        throw new Error("Java bridge unavailable");
    }

    var className = args.className;

    // Fast path: default loader works.
    try {
        var defaultClass = Java.use(className);
        return {
            ok:          true,
            className:   className,
            loader:      null,
            loaderDesc:  "<default>",
            viaDefault:  true
        };
    } catch (e) {
        // fall through
    }

    // Cache hit for a non-default loader.
    var cached = g_class_loader_cache[className];
    if (cached) {
        try {
            var loader = Java.cast(ptr(cached.handle), Java.use("java.lang.ClassLoader"));
            var factory = Java.ClassFactory.get(loader);
            factory.use(className);
            return {
                ok:         true,
                className:  className,
                loader:     cached.handle,
                loaderDesc: cached.description,
                viaDefault: false,
                cached:     true
            };
        } catch (e) {
            delete g_class_loader_cache[className];
        }
    }

    // Try every loader.
    var loaders = [];
    try {
        Java.performNow(function () {
            Java.enumerateClassLoaders({
                onMatch: function (l) {
                    try {
                        var handle = Java.retain(l).$h.toString();
                        loaders.push({ handle: handle, loader: l });
                    } catch (e) {}
                },
                onComplete: function () {}
            });
        });
    } catch (e) {
        throw new Error("could not enumerate class loaders: " + String(e && e.message));
    }

    for (var i = 0; i < loaders.length; i++) {
        try {
            var factory = Java.ClassFactory.get(loaders[i].loader);
            factory.use(className);
            g_class_loader_cache[className] = {
                handle:      loaders[i].handle,
                description: String(loaders[i].loader)
            };
            return {
                ok:         true,
                className:  className,
                loader:     loaders[i].handle,
                loaderDesc: String(loaders[i].loader),
                viaDefault: false,
                cached:     false,
                totalLoadersTried: loaders.length
            };
        } catch (e) {
            // try next loader
        }
    }

    throw new Error(
        "class not found in any loader: " + className +
        " (tried " + loaders.length + " loaders)");
};

// Invalidate the cache when the caller switches to a different target.
handlers["class.resolve.clear_cache"] = function () {
    g_class_loader_cache = {};
    return { ok: true };
};

