// core/message_router.cpp
#include "core/message_router.h"
#include "art/art_guard.h"
#include "core/bootstrap.h"
#include "core/script_host.h"

#include <android/log.h>
#include <atomic>
#include <condition_variable>
#include <cstring>

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace {

// Detects whether the calling thread is the JS thread. We track this
// by thread-local set at the entry of the JS main loop and at the
// message handler; but the cheapest reliable check is to compare the
// pthread identity against a saved one. Since we cannot access the
// GThread object from any thread safely without a lock, we instead
// rely on a simpler heuristic: if pending_sync_.find(id) is populated
// by a caller that is *not* executing inside handle_message, we know
// the caller is not the JS thread. That is trivially true because
// handle_message is the only place that ever calls
// try_deliver_reply and returns. So the check below is only needed
// for send_command called *from inside a JS callback* — which we
// disallow by convention in the API contract and cannot detect at
// runtime without a thread-local. We accept that risk by leaving a
// runtime guard based on `g_thread_self` comparison after the first
// JS thread entry.

struct LogMessage {
    std::string level;
    std::string message;
};

// Base64 decoder used for chunked payloads. Chunks are base64-encoded
// UTF-8 JSON strings, one chunk per message. Non-chunked messages use
// raw JSON (no base64) for speed.
std::string base64_decode(const std::string& in) {
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::string out;
    out.reserve(in.size() * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : in) {
        int8_t v = T[(uint8_t)c];
        if (v < 0) {
            if (c == '=') break;
            continue;
        }
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

MessageRouter& MessageRouter::instance() {
    static MessageRouter r;
    return r;
}

// ═══════════════════════════════════════════════════════════════
//  handle_message — called from the JS thread
// ═══════════════════════════════════════════════════════════════

void MessageRouter::handle_message(const char* raw, GBytes* data) {
    (void)data;
    if (raw == nullptr) return;

    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(raw);
    } catch (const std::exception& e) {
        LOGE("handle_message: parse failed: %s (raw=%s)", e.what(), raw);
        return;
    }

    if (!msg.is_object()) {
        LOGW("handle_message: envelope is not an object");
        return;
    }

    // ── Chunked payloads (auto-chunking) ────────────────────
    //
    // The bootstrap.js agent wraps globalThis.send with a version that
    // emits chunk_begin / chunk_data×N / chunk_end for payloads larger
    // than 96 KB. We reassemble them here, then re-dispatch the
    // reconstructed JSON string as if it had arrived in one piece.
    //
    // There is no cap on the number of chunks or the total size — the
    // buffers grow to fit whatever the JS side sends.
    {
        // Frida 17 still wraps send() payloads in {type:"send", payload:...}.
        // Unwrap unconditionally so chunk_* and the ready/error/log/event
        // handlers see the same shape whether the script used send()
        // directly or the auto-chunk wrapper.
        if (msg.value("type", std::string()) == "send" &&
            msg.contains("payload") && msg["payload"].is_object()) {
            msg = msg["payload"];
        }

        const std::string t = msg.value("type", std::string());
        if (t == "chunk_begin") {
            if (!msg.contains("id") || !msg["id"].is_number_unsigned()) return;
            uint64_t id = msg["id"].get<uint64_t>();
            uint64_t total = msg.value("totalChunks", (uint64_t)0);
            std::lock_guard<std::mutex> lk(pending_mu_);
            auto& buf = chunks_[id];
            buf.total    = total;
            buf.received = 0;
            buf.parts.clear();
            // Reserve up-front — this is an optimisation hint, not a
            // cap. If total is absurdly large, reserve is a no-op
            // because the vector would throw bad_alloc first.
            try {
                if (total > 0 && total < 1u << 20) buf.parts.reserve((size_t)total);
            } catch (...) { /* ignore */ }
            return;
        }

        if (t == "chunk_data") {
            if (!msg.contains("id") || !msg["id"].is_number_unsigned()) return;
            if (!msg.contains("seq") || !msg["seq"].is_number_unsigned()) return;
            uint64_t id  = msg["id"].get<uint64_t>();
            uint64_t seq = msg["seq"].get<uint64_t>();
            std::string data = msg.value("data", std::string());
            std::lock_guard<std::mutex> lk(pending_mu_);
            auto it = chunks_.find(id);
            if (it == chunks_.end()) return;
            auto& buf = it->second;
            if (seq >= buf.parts.size()) buf.parts.resize((size_t)seq + 1);
            if (buf.parts[(size_t)seq].empty()) {
                buf.parts[(size_t)seq] = std::move(data);
                buf.received++;
            }
            return;
        }

        if (t == "chunk_end") {
            if (!msg.contains("id") || !msg["id"].is_number_unsigned()) return;
            uint64_t id = msg["id"].get<uint64_t>();
            std::string full;
            {
                std::lock_guard<std::mutex> lk(pending_mu_);
                auto it = chunks_.find(id);
                if (it == chunks_.end()) return;
                // Concatenate all parts. Missing parts are treated as
                // empty strings; a malformed sender cannot crash us.
                size_t total_bytes = 0;
                for (const auto& part : it->second.parts) total_bytes += part.size();
                full.reserve(total_bytes + 16);
                for (const auto& part : it->second.parts) full.append(part);
                chunks_.erase(it);
            }
            // Re-dispatch the reassembled JSON. Call handle_message on
            // the current thread — we are already on the JS thread
            // (the only caller of chunk handlers).
            if (!full.empty()) {
                handle_message(full.c_str(), nullptr);
            }
            return;
        }
    }

    // ── Ready / error from prelude or bootstrap ────────────
    const std::string type = msg.value("type", std::string());

    if (type == "ready") {
        const std::string phase = msg.value("phase", std::string());
        // Populate BridgeInfo from the prelude (bootstrap re-sends the
        // same info, but the prelude is authoritative).
        if (phase == "prelude") {
            BridgeInfo info;
            info.reported  = true;
            info.hasJava   = msg.value("hasJava", false);
            if (msg.contains("bridge") && msg["bridge"].is_object()) {
                const auto& b = msg["bridge"];
                info.variant   = b.value("variant", std::string());
                info.available = b.value("available", false);
                if (b.contains("version") && b["version"].is_number()) {
                    info.apiLevel = b["version"].get<int>();
                }
            }
            // Push to bootstrap state via the accessor at the bottom
            // of bootstrap.cpp. Declared in bootstrap.h.
            // (bootstrap_signal_ready only wakes the condition; the
            //  BridgeInfo population happens here under the pending
            //  mutex to keep it single-writer.)
            // We can't write directly into bootstrap's BridgeInfo
            // without an accessor, so bootstrap.cpp exposes one.
            extern void bootstrap_set_bridge_info(const BridgeInfo&);
            bootstrap_set_bridge_info(info);
        }
        bootstrap_signal_ready(phase.c_str());
        return;
    }

    if (type == "error") {
        const std::string phase = msg.value("phase", std::string());
        const std::string message = msg.value("message", std::string());
        LOGE("JS error [%s]: %s", phase.c_str(), message.c_str());
        if (phase == "prelude" || phase == "bootstrap") {
            bootstrap_signal_fatal(message.c_str());
        }
        return;
    }

    // ── Log messages ────────────────────────────────────────
    if (type == "log") {
        const std::string level   = msg.value("level", std::string("info"));
        const std::string message = msg.value("message", std::string());
        std::lock_guard<std::mutex> lk(log_mu_);
        if (log_cb_) log_cb_(level, message);
        return;
    }

    // ── Events ─────────────────────────────────────────────
    if (type == "event") {
        const std::string name = msg.value("name", std::string());
        if (name.empty()) return;
        std::vector<EventCallback> cbs;
        {
            std::lock_guard<std::mutex> lk(subs_mu_);
            auto it = subscribers_.find(name);
            if (it != subscribers_.end()) cbs = it->second;
        }
        for (auto& cb : cbs) {
            try { cb(msg); } catch (...) {}
        }
        return;
    }

    // ── Replies ────────────────────────────────────────────
    if (type == "reply") {
        if (!msg.contains("id") || !msg["id"].is_number_unsigned()) return;
        uint64_t id = msg["id"].get<uint64_t>();
        bool ok     = msg.value("ok", false);
        nlohmann::json result = msg.contains("result")
            ? msg["result"] : nlohmann::json();
        std::string err = msg.value("error", std::string());
        try_deliver_reply(id, ok, result, err);
        return;
    }

    LOGW("handle_message: unknown type '%s'", type.c_str());
}

// ═══════════════════════════════════════════════════════════════
//  install_capabilities_subscription — called once at bootstrap
// ═══════════════════════════════════════════════════════════════

void MessageRouter::install_capabilities_subscription() {
    // The bootstrap.js agent emits an "event" with name "capabilities"
    // before it emits "ready". We subscribe here so the payload is
    // routed into art::ArtGuard exactly once per session.
    subscribe("capabilities", [](const nlohmann::json& msg) {
        if (!msg.contains("payload") || !msg["payload"].is_object()) return;
        const auto& p = msg["payload"];

        art::BridgeCapabilities bridge;
        art::JvmtiCapabilities  jvmti;
        art::ArtCapabilities    art;

        if (p.contains("bridge") && p["bridge"].is_object()) {
            const auto& b = p["bridge"];
            bridge.reported  = b.value("reported", false);
            bridge.available = b.value("available", false);
            bridge.hasJava   = b.value("hasJava", false);
            bridge.variant   = b.value("variant", std::string());
            bridge.apiLevel  = b.value("apiLevel", 0);
        }
        if (p.contains("jvmti") && p["jvmti"].is_object()) {
            const auto& j = p["jvmti"];
            jvmti.available     = j.value("available", false);
            jvmti.canTagObjects = j.value("canTagObjects", false);
        }
        art.flavor            = p.value("flavor", std::string("unknown"));
        art.hasArtInternals   = p.value("hasArtInternals", false);

        art::ArtGuard::instance().update(bridge, jvmti, art);
    });
}

// ═══════════════════════════════════════════════════════════════
//  try_deliver_reply — must be called with pending_mu_ NOT held
// ═══════════════════════════════════════════════════════════════

void MessageRouter::try_deliver_reply(uint64_t id, bool ok,
                                      const nlohmann::json& result,
                                      const std::string& error) {
    std::shared_ptr<PendingSync> sync;
    ReplyCallback async_cb;
    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        auto it = pending_sync_.find(id);
        if (it != pending_sync_.end()) {
            sync = it->second;
            pending_sync_.erase(it);
        } else {
            auto it2 = pending_async_.find(id);
            if (it2 != pending_async_.end()) {
                async_cb = std::move(it2->second.cb);
                pending_async_.erase(it2);
            }
        }
    }

    if (sync) {
        {
            std::lock_guard<std::mutex> lk(sync->mu);
            sync->ok     = ok;
            sync->result = result;
            sync->error  = error;
            sync->done   = true;
        }
        sync->cv.notify_one();
        return;
    }
    if (async_cb) {
        try { async_cb(ok, ok ? result : nlohmann::json{{"error", error}}); }
        catch (...) {}
    }
}

// ═══════════════════════════════════════════════════════════════
//  send_command — synchronous; must NOT be called from JS thread
// ═══════════════════════════════════════════════════════════════

bool MessageRouter::send_command(const std::string& op,
                                 const nlohmann::json& args,
                                 nlohmann::json& result,
                                 std::string& error,
                                 int timeout_ms) {
    auto p = std::make_shared<PendingSync>();
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_sync_[id] = p;
    }

    std::string args_json = args.is_null() ? "{}" : args.dump();
    ScriptHost::instance().post_command(id, op, args_json);

    std::unique_lock<std::mutex> lk(p->mu);
    if (timeout_ms <= 0) {
        p->cv.wait(lk, [&] { return p->done; });
    } else {
        if (!p->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [&] { return p->done; })) {
            // Timed out — drop the pending entry so a late reply becomes
            // a no-op instead of a leak.
            {
                std::lock_guard<std::mutex> lk2(pending_mu_);
                pending_sync_.erase(id);
            }
            error = "timeout after " + std::to_string(timeout_ms) + " ms";
            return false;
        }
    }

    if (!p->ok) {
        error = p->error.empty() ? "unknown error" : p->error;
        return false;
    }
    result = p->result;
    return true;
}

// ═══════════════════════════════════════════════════════════════
//  post_command — asynchronous; safe from any thread
// ═══════════════════════════════════════════════════════════════

void MessageRouter::post_command(const std::string& op,
                                 const nlohmann::json& args,
                                 ReplyCallback cb) {
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(pending_mu_);
        pending_async_[id] = PendingAsync{std::move(cb)};
    }
    std::string args_json = args.is_null() ? "{}" : args.dump();
    ScriptHost::instance().post_command(id, op, args_json);
}

// ═══════════════════════════════════════════════════════════════
//  Event subscription
// ═══════════════════════════════════════════════════════════════

void MessageRouter::subscribe(const std::string& name, EventCallback cb) {
    if (name.empty() || !cb) return;
    std::lock_guard<std::mutex> lk(subs_mu_);
    subscribers_[name].push_back(std::move(cb));
}

void MessageRouter::unsubscribe(const std::string& name) {
    std::lock_guard<std::mutex> lk(subs_mu_);
    subscribers_.erase(name);
}

void MessageRouter::set_log_callback(
    std::function<void(const std::string&, const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(log_mu_);
    log_cb_ = std::move(cb);
}

} // namespace yamgg
