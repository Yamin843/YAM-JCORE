//
// LIBYAMJS's send() delivers a single message through the Gum
// message channel, which has practical size constraints on some
// Android builds. To guarantee that any payload — regardless of size —
// is delivered intact, we replace globalThis.send with a wrapper that:
//
//   1. Serializes the payload to JSON to measure it.
//   2. If the JSON is below the transport chunk size (96 KB is chosen
//      to comfortably fit inside any plausible transport buffer), the
//      wrapper calls the original send() directly.
//   3. Otherwise, the wrapper slices the JSON string into segments and
//      sends chunk_begin / chunk_data×N / chunk_end messages. The C++
//      side (message_router.cpp) reassembles them and re-dispatches the
//      reconstructed JSON as if it had arrived in one piece.
//
// There is NO upper bound on the total payload size — the number of
// chunks is limited only by available memory.
const AUTO_CHUNK_THRESHOLD = 96 * 1024;
var g_chunk_counter = 0;

(function installAutoChunking() {
    var originalSend = globalThis.send;
    if (typeof originalSend !== "function") return;

    globalThis.send = function (payload, data) {
        // Binary data always goes through the original path: it is not
        // serialized, so the chunking logic does not apply.
        if (data) {
            originalSend(payload, data);
            return;
        }

        var json;
        try {
            json = JSON.stringify(payload);
        } catch (e) {
            // Circular reference, BigInt, or a value JSON.stringify
            // refuses. Fall back to the original send; the caller will
            // get the error through the message channel, which is
            // preferable to silently dropping the message.
            originalSend(payload, null);
            return;
        }

        if (typeof json !== "string" || json.length <= AUTO_CHUNK_THRESHOLD) {
            originalSend(payload, null);
            return;
        }

        var id = ++g_chunk_counter;
        var totalChunks = Math.ceil(json.length / AUTO_CHUNK_THRESHOLD);

        originalSend({
            type: "chunk_begin",
            id: id,
            totalChunks: totalChunks
        }, null);

        for (var i = 0; i < totalChunks; i++) {
            var start = i * AUTO_CHUNK_THRESHOLD;
            var end = Math.min(start + AUTO_CHUNK_THRESHOLD, json.length);
            originalSend({
                type: "chunk_data",
                id: id,
                seq: i,
                data: json.substring(start, end)
            }, null);
        }

        originalSend({ type: "chunk_end", id: id }, null);
    };
})();
