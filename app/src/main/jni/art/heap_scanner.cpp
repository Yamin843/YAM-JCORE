// art/heap_scanner.cpp
#include "art/heap_scanner.h"
#include "art/art_guard.h"
#include "core/message_router.h"
#include "core/thread_sync.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace art {

namespace {

// Session contains a std::atomic<bool> and is therefore non-copyable.
// We store shared_ptr<Session> in the map instead of Session directly,
// which makes both insertion and lookup cheap and copy-free.
struct Session {
    HeapScanner::MatchCallback        on_match;
    HeapScanner::CompleteCallback     on_complete;
    std::shared_ptr<std::atomic<bool>> aborted;
};

struct Registry {
    std::mutex                                    mu;
    std::map<uint64_t, std::shared_ptr<Session>>  sessions;
    std::atomic<uint64_t>                         next_id{1};
    bool                                          installed = false;
};

Registry g_reg;

void deliver_match(uint64_t session_id, const nlohmann::json& p) {
    std::shared_ptr<Session> sess;
    {
        std::lock_guard<std::mutex> lk(g_reg.mu);
        auto it = g_reg.sessions.find(session_id);
        if (it == g_reg.sessions.end()) return;
        sess = it->second;   // shared_ptr copy, cheap
    }

    if (!sess || !sess->on_match) return;
    if (sess->aborted && sess->aborted->load(std::memory_order_acquire)) {
        return;
    }

    HeapMatch m;
    m.sessionId = session_id;
    m.className = p.value("className", std::string());
    m.handle    = p.value("handle",    std::string());
    m.stringRep = p.value("toString",  std::string());

    try { sess->on_match(m); } catch (...) {}
}

void deliver_complete(uint64_t session_id) {
    std::shared_ptr<Session> sess;
    {
        std::lock_guard<std::mutex> lk(g_reg.mu);
        auto it = g_reg.sessions.find(session_id);
        if (it == g_reg.sessions.end()) return;
        sess = it->second;
        g_reg.sessions.erase(it);
    }

    if (!sess || !sess->on_complete) return;
    bool was_aborted = sess->aborted &&
                       sess->aborted->load(std::memory_order_acquire);

    try { sess->on_complete(session_id, was_aborted); } catch (...) {}
}

void ensure_subscriptions() {
    if (g_reg.installed) return;
    g_reg.installed = true;

    MessageRouter::instance().subscribe("heap.match",
        [](const nlohmann::json& msg) {
            if (!msg.contains("payload")) return;
            const auto& p = msg["payload"];
            uint64_t session = p.value("sessionId", (uint64_t)0);
            if (session != 0) deliver_match(session, p);
        });

    MessageRouter::instance().subscribe("heap.complete",
        [](const nlohmann::json& msg) {
            if (!msg.contains("payload")) return;
            const auto& p = msg["payload"];
            uint64_t session = p.value("sessionId", (uint64_t)0);
            if (session != 0) deliver_complete(session);
        });

    MessageRouter::instance().subscribe("heap.visit_complete",
        [](const nlohmann::json& msg) {
            if (!msg.contains("payload")) return;
            const auto& p = msg["payload"];
            uint64_t session = p.value("sessionId", (uint64_t)0);
            if (session != 0) deliver_complete(session);
        });

    // Progress events are logged to logcat but not delivered to
    // callbacks (the callback signatures are Match and Complete only).
    // Future: extend HeapScanner with a ProgressCallback.
    MessageRouter::instance().subscribe("heap.progress",
        [](const nlohmann::json& msg) {
            if (!msg.contains("payload")) return;
            const auto& p = msg["payload"];
            uint64_t session = p.value("sessionId", (uint64_t)0);
            uint64_t matches = p.value("matches", (uint64_t)0);
            uint64_t elapsed = p.value("elapsedMs", (uint64_t)0);
            __android_log_print(ANDROID_LOG_DEBUG, "YAMGG",
                "heap.progress session=%llu matches=%llu elapsed=%llums",
                (unsigned long long)session,
                (unsigned long long)matches,
                (unsigned long long)elapsed);
        });
}

} // namespace

HeapScanner& HeapScanner::instance() {
    static HeapScanner s;
    return s;
}

uint64_t HeapScanner::choose(const std::string& className,
                             MatchCallback onMatch,
                             CompleteCallback onComplete,
                             std::string& error,
                             int timeout_ms) {
    if (!ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return 0;
    }
    if (className.empty()) {
        error = "class name required";
        return 0;
    }

    ensure_subscriptions();

    uint64_t id = g_reg.next_id.fetch_add(1, std::memory_order_relaxed);

    auto sess = std::make_shared<Session>();
    sess->on_match    = std::move(onMatch);
    sess->on_complete = std::move(onComplete);
    sess->aborted     = std::make_shared<std::atomic<bool>>(false);

    {
        std::lock_guard<std::mutex> lk(g_reg.mu);
        g_reg.sessions[id] = sess;
    }

    nlohmann::json args = {{"className", className}, {"sessionId", id}};
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("heap.choose",
                                                args, result, error, timeout_ms)) {
        std::lock_guard<std::mutex> lk(g_reg.mu);
        g_reg.sessions.erase(id);
        return 0;
    }

    if (!result.value("started", false)) {
        error = result.value("error", std::string("choose failed to start"));
        std::lock_guard<std::mutex> lk(g_reg.mu);
        g_reg.sessions.erase(id);
        return 0;
    }

    return id;
}

bool HeapScanner::cancel(uint64_t sessionId) {
    std::lock_guard<std::mutex> lk(g_reg.mu);
    auto it = g_reg.sessions.find(sessionId);
    if (it == g_reg.sessions.end()) return false;
    if (it->second && it->second->aborted) {
        it->second->aborted->store(true, std::memory_order_release);
    }
    return true;
}

} // namespace art
} // namespace yamgg
