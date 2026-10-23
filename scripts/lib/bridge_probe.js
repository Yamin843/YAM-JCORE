var bridgeInfo = { reported: false, available: false, variant: "", apiLevel: 0, hasJava: false };
try {
    if (typeof globalThis.__yamgg_bridge_ready__ === "function") {
        var b = globalThis.__yamgg_bridge_ready__();
        if (b) {
            bridgeInfo.reported  = true;
            bridgeInfo.available = !!b.available;
            bridgeInfo.variant   = b.variant || "";
            bridgeInfo.apiLevel  = b.version || 0;
        }
    }
} catch (e) {
    send({ type: "log", level: "error",
           message: "bridge probe failed: " + safeString(e) });
}
bridgeInfo.hasJava = (typeof globalThis.Java === "object" && globalThis.Java !== null);
