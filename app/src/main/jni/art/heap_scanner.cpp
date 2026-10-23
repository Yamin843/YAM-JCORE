// art/heap_scanner.cpp
#include "art/heap_scanner.h"
#include "art/art_guard.h"
#include "core/message_router.h"
#include "core/thread_sync.h"

#include <atomic>
#include <map>
#include <mutex>

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace art {

namespace {

struct Session {
    MatchCallback        on_match;
    CompleteCallback     on_complete;
    std::atomic<bool>    aborted{false};
};

struct Registry {
    std::mutex                      mu;
    std::map<uint64_t, Session>     sessions;
    std::atomic<uint64_t>           next_id{1};
    bool                            installed = false;
};

Registry g_reg;

void ensure_subscriptions() {
    if (g_reg.installed) return;
    g_reg.installed = true;

    MessageRouter::instance().subscribe("heap.match",
        [](const nlohmann::json& msg) {
            if (!msg.contains("payload")) return;
            const auto& p = msg["payload"];
            uint64_t session = p.value("sessionId", (uint64_t)0);
            HeapMatch m;
            m.sessionId   = session;
            m.className   = p.value("className", std::string());
            m.handle      = p.value("handle", std::string());
            m.stringRep   = p.value("toString", std::string());

            Session copy;
            bool have = false;
            {
                std::lock_guard<std::mutex> lk(g_reg.mu);
                auto it = g_reg.sessions.find(session);
                if (it != g_reg.sessions.end()) {
                    copy = it->second;
                    have = true;
                }
            }
            if (have && copy.on_match && !copy.aborted.load()) {
                try { copy.on_match(m); } catch (...) {}
            }
        });

    MessageRouter::instance().subscribe("heap.complete",
        [](const nlohmann::json& msg) {
            if (!msg.contains("payload")) return;
            const auto& p = msg["payload"];
            uint64_t session = p.value("sessionId", (uint64_t)0);

            Session copy;
            bool have = false;
            {
                std::lock_guard<std::mutex> lk(g_reg.mu);
                auto it = g_reg.sessions.find(session);
                if (it != g_reg.sessions.end()) {
                    copy = it->second;
                    have = true;
                    g_reg.sessions.erase(it);
                }
            }
            if (have && copy.on_complete) {
                try { copy.on_complete(session, copy.aborted.load()); }
                catch (...) {}
            }
        });

    // "heap.visit_complete" fires for the visit_objects path; treat it
    // identically to heap.complete.
    MessageRouter::instance().subscribe("heap.visit_complete",
        [](const nlohmann::json& msg) {
            if (!msg.contains("payload")) return;
            const auto& p = msg["payload"];
            uint64_t session = p.value("sessionId", (uint64_t)0);

            Session copy;
            bool have = false;
            {
                std::lock_guard<std::mutex> lk(g_reg.mu);
                auto it = g_reg.sessions.find(session);
                if (it != g_reg.sessions.end()) {
                    copy = it->second;
                    have = true;
                    g_reg.sessions.erase(it);
                }
            }
            if (have && copy.on_complete) {
                try { copy.on_complete(session, copy.aborted.load()); }
                catch (...) {}
            }
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
    {
        std::lock_guard<std::mutex> lk(g_reg.mu);
        g_reg.sessions[id] = Session{std::move(onMatch), std::move(onComplete), {false}};
    }

    nlohmann::json args = {{"className", className}, {"sessionId", id}};
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("heap.choose",
                                                args, result, error, timeout_ms)) {
        // Roll back the registration. The completion callback will not
        // fire, so callers must not expect one on failure.
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
    it->second.aborted.store(true);
    // Java.choose has no cancel; we simply stop delivering matches.
    // The complete callback will still fire from JS when choose
    // finishes naturally. That callback will see aborted=true and
    // can report the cancellation to the user.
    return true;
}

} // namespace art
} // namespace yamgg
