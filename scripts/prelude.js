// scripts/prelude.js — first script loaded into the LIBYAMJS runtime.
//
// Runs before bootstrap. Reports the bridge status once, then returns.
// The C++ side (bootstrap.cpp) waits for the "ready" message before
// loading bootstrap.js, so a broken prelude cannot proceed to a
// half-initialised bootstrap session.
(function () {
    "use strict";

    var bridge = null;
    try {
        if (typeof globalThis.__yamgg_bridge_ready__ === "function") {
            bridge = globalThis.__yamgg_bridge_ready__();
        }
    } catch (e) {
        send({
            type: "error",
            phase: "prelude",
            message: String((e && e.message) || e)
        });
        return;
    }

    send({
        type: "ready",
        phase: "prelude",
        hasJava: (typeof globalThis.Java === "object"),
        bridge: bridge ? {
            variant:   bridge.variant,
            available: bridge.available,
            version:   bridge.version
        } : null
    });
})();
