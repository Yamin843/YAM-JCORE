// scripts/prelude.js — first script loaded into the LIBYAMJS runtime.
//
// Runs before bootstrap. Reports the bridge status once, then returns.
// The C++ side (bootstrap.cpp) waits for the "ready" message before
// loading bootstrap.js, so a broken prelude cannot proceed to a
// half-initialised bootstrap session.
(function () {
    "use strict";

    // Do NOT call __yamgg_bridge_ready__() here. That function
    // touches Java.available, which tries to initialize the bridge
    // against libart. If libart has not finished loading yet (late
    // ART init, embedded libart, etc.), this crashes the process.
    //
    // The bridge is initialized later by bootstrap.cpp's
    // intelligent_initialize(), which polls for libart first.
    var hasJava = (typeof globalThis.Java === "object" && globalThis.Java !== null);

    send({
        type: "ready",
        phase: "prelude",
        hasJava: hasJava,
        bridge: null   // populated later, after libart is confirmed
    });
})();
