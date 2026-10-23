
// ═══════════════════════════════════════════════════════════════════
//  smart_init.js — prevent bridge initialization before libart exists
// ═══════════════════════════════════════════════════════════════════
//
// The bridge caches its "Java.available" answer on first read. If the
// first read happens before libart.so is loaded (late app startup,
// embedded ART, etc.), the bridge fails permanently. This module runs
// BEFORE any code that touches Java.available, polls libart using
// Gum APIs only, and only then triggers bridge initialization.
//
// The decision loop:
//   1. Poll Process.enumerateModules() every 200 ms (Gum-only, safe).
//   2. When a module matches /lib(art|dvm)\.so$/, probe it for
//      JNI_GetCreatedJavaVMs (present in every ART build).
//   3. If the probe succeeds, read Java.available (which now
//      succeeds and caches correctly).
//   4. Otherwise, report a precise failure reason.

(function installSmartInit() {

    globalThis.__yamgg_smart_init_state = "pending";
    globalThis.__yamgg_smart_init_info = null;

    function findLibartCandidates() {
        var candidates = [];
        try {
            var modules = Process.enumerateModules();
            for (var i = 0; i < modules.length; i++) {
                var m = modules[i];
                if (/^lib(art|dvm)\.so$/.test(m.name)) {
                    candidates.push(m);
                } else if (m.path && (
                        m.path.indexOf("/art/") !== -1 ||
                        m.path.indexOf("libart") !== -1)) {
                    if (/lib(art|dvm)[^/]*\.so$/.test(m.name)) {
                        candidates.push(m);
                    }
                }
            }
        } catch (e) {}
        return candidates;
    }

    function probeCandidate(m) {
        // A usable libart must export JNI_GetCreatedJavaVMs. Everything
        // else (ART internals) may or may not exist depending on the
        // build; the bridge already handles those gracefully.
        try {
            var sym = m.findExportByName("JNI_GetCreatedJavaVMs");
            if (sym) return { ok: true, symbol: sym };
        } catch (e) {}
        // Fallback: try the global lookup.
        try {
            var sym2 = Module.findGlobalExportByName("JNI_GetCreatedJavaVMs");
            if (sym2) return { ok: true, symbol: sym2 };
        } catch (e) {}
        return { ok: false };
    }

    function tryBridge() {
        try {
            var info = globalThis.__yamgg_bridge_ready__();
            return { ok: !!(info && info.available), info: info };
        } catch (e) {
            return { ok: false, error: String((e && e.message) || e) };
        }
    }

    globalThis.handlers = globalThis.handlers || {};
    handlers["smart_init.start"] = function (args) {
        var timeoutMs   = (args && args.timeoutMs) || 30000;
        var intervalMs  = (args && args.intervalMs) || 200;
        var started     = Date.now();
        var found       = [];

        function attempt() {
            if (globalThis.__yamgg_smart_init_state !== "pending") {
                return;   // already resolved
            }

            var candidates = findLibartCandidates();

            // Diagnostic: record every candidate we have ever seen.
            for (var i = 0; i < candidates.length; i++) {
                var path = candidates[i].path;
                var already = false;
                for (var j = 0; j < found.length; j++) {
                    if (found[j].path === path) { already = true; break; }
                }
                if (!already) {
                    found.push({ path: path, name: candidates[i].name });
                    send({ type: "event", name: "smart_init.candidate",
                           payload: { path: path, name: candidates[i].name } });
                }
            }

            for (var k = 0; k < candidates.length; k++) {
                var cand = candidates[k];
                var probe = probeCandidate(cand);
                if (probe.ok) {
                    var bridge = tryBridge();
                    if (bridge.ok) {
                        globalThis.__yamgg_smart_init_state = "ready";
                        globalThis.__yamgg_smart_init_info = {
                            mode: "full",
                            libartPath: cand.path,
                            libartName: cand.name,
                            candidateCount: candidates.length,
                            elapsedMs: Date.now() - started
                        };
                        send({ type: "event", name: "smart_init.ready",
                               payload: globalThis.__yamgg_smart_init_info });
                        return;
                    }
                    // libart valid but bridge failed. Record and give
                    // the bridge one more chance (in case the failure
                    // was transient). But since the bridge caches, we
                    // cannot force a retry — we just report the error.
                    globalThis.__yamgg_smart_init_state = "failed";
                    globalThis.__yamgg_smart_init_info = {
                        mode: "jni_only_fallback",
                        reason: "bridge_init_failed",
                        error: bridge.error || "available=false",
                        libartPath: cand.path,
                        libartName: cand.name
                    };
                    send({ type: "event", name: "smart_init.failed",
                           payload: globalThis.__yamgg_smart_init_info });
                    return;
                }
            }

            if (Date.now() - started >= timeoutMs) {
                globalThis.__yamgg_smart_init_state = "failed";
                globalThis.__yamgg_smart_init_info = {
                    mode: "dead",
                    reason: candidates.length === 0
                        ? "no_libart_module_found"
                        : "libart_found_but_no_JNI_GetCreatedJavaVMs",
                    elapsedMs: Date.now() - started,
                    candidates: found
                };
                send({ type: "event", name: "smart_init.failed",
                       payload: globalThis.__yamgg_smart_init_info });
                return;
            }

            setTimeout(attempt, intervalMs);
        }

        // Kick off asynchronously so the script's synchronous tail
        // can finish loading the rest of the handlers.
        setTimeout(attempt, 10);
        return { started: true };
    };

    // Diagnostic handler, callable from the UI.
    handlers["smart_init.status"] = function () {
        return {
            state: globalThis.__yamgg_smart_init_state,
            info:  globalThis.__yamgg_smart_init_info
        };
    };

})();

