
// ═══════════════════════════════════════════════════════════════════
//  art_diagnostics.js — exhaustive libart / classloader inspection
// ═══════════════════════════════════════════════════════════════════
//
// Called from the UI's Settings tab (or programmatically) to build a
// full picture of the ART state. The result drives the operator's
// decision when the bridge reports a failure: is this an app with its
// own libart? Multiple libart? A renamed ART? A hidden symbol?

handlers["art.diagnose"] = function () {
    var report = {
        libartCandidates:  [],
        jvmModules:        [],
        classLoaderCount:  0,
        bridgeState:       globalThis.__yamgg_smart_init_state || "unknown",
        apiLevel:          0
    };

    // 1. Find every module that looks like libart or libjvm.
    try {
        var modules = Process.enumerateModules();
        for (var i = 0; i < modules.length; i++) {
            var m = modules[i];
            var isArt = /^lib(art|dvm)\.so$/.test(m.name);
            var isJvm = /libjvm\.so$/.test(m.name);
            var looksArt = /libart/i.test(m.name) ||
                           (m.path && m.path.indexOf("/art/") !== -1);

            if (isArt || isJvm || looksArt) {
                var cand = {
                    name:        m.name,
                    path:        m.path,
                    base:        m.base.toString(),
                    size:        m.size,
                    isArtExact:  isArt,
                    isJvmExact:  isJvm,
                    hasGetVMs:   false,
                    hasArtSymbols: 0
                };
                try {
                    if (m.findExportByName("JNI_GetCreatedJavaVMs")) {
                        cand.hasGetVMs = true;
                    }
                } catch (e) {}
                // Sample how many art:: symbols the module exports.
                try {
                    var exports = m.enumerateExports();
                    for (var j = 0; j < exports.length; j++) {
                        if (exports[j].name.indexOf("_ZN3art") === 0) {
                            cand.hasArtSymbols++;
                            if (cand.hasArtSymbols >= 20) break;
                        }
                    }
                } catch (e) {}

                if (isArt)          report.libartCandidates.push(cand);
                else if (isJvm)     report.jvmModules.push(cand);
                else                report.libartCandidates.push(cand);
            }
        }
    } catch (e) {
        report.enumerateError = String(e && e.message);
    }

    // 2. Count class loaders, if the bridge is available.
    if (globalThis.Java && Java.available) {
        try {
            report.apiLevel = Java.androidVersion;
            Java.performNow(function () {
                Java.enumerateClassLoaders({
                    onMatch: function (l) { report.classLoaderCount++; },
                    onComplete: function () {}
                });
            });
        } catch (e) {
            report.javaError = String(e && e.message);
        }
    }

    return report;
};

// Diagnostic: list every loaded class from every loader.
handlers["art.classes_from_all_loaders"] = function (args) {
    if (!globalThis.Java || !Java.available) {
        throw new Error("Java bridge unavailable");
    }
    var filter = (args && args.filter) || "";
    var result = { loaders: [], totalClasses: 0 };

    Java.performNow(function () {
        var seen = {};
        Java.enumerateClassLoaders({
            onMatch: function (loader) {
                var entry = {
                    description: String(loader),
                    classes: []
                };
                try {
                    var factory = Java.ClassFactory.get(loader);
                    // We cannot enumerate a factory's classes directly,
                    // but we can test whether known classes load
                    // through it. Callers use this to find which
                    // loader owns a given class.
                } catch (e) {
                    entry.error = String(e && e.message);
                }
                result.loaders.push(entry);
            },
            onComplete: function () {}
        });
    });

    // Also enumerate classes globally (union of all loaders).
    var allClasses = [];
    try {
        Java.enumerateLoadedClasses({
            onMatch: function (name) {
                if (!filter || name.indexOf(filter) !== -1) {
                    allClasses.push(name);
                }
            },
            onComplete: function () {}
        });
    } catch (e) {}
    result.totalClasses = allClasses.length;
    result.classes = allClasses.slice(0, 5000);   // avoid huge payloads
    result.truncated = allClasses.length > 5000;

    return result;
};

