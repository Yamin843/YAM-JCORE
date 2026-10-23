// core/message_router.h — the C++ ↔ JS protocol.
//
// NOT the frida:rpc protocol. That protocol exists so a client on a
// socket can talk to a server on a socket. Both endpoints here live in
// the same process and share the same address space, so we use a
// direct, synchronous map instead of the RPC handshake.
//
// Protocol:
//
//   C++ → JS:   {"type":"cmd","id":N,"op":"OP","args":{…}}
//   JS  → C++:  {"type":"reply","id":N,"ok":true,"result":{…}}
//               {"type":"reply","id":N,"ok":false,"error":"…"}
//               {"type":"event","name":"NAME","payload":{…}}
//               {"type":"log","level":"info","message":"…"}
//               {"type":"ready","phase":"prelude"|"bootstrap", …}
//               {"type":"error","phase":"prelude"|"bootstrap","message":"…"}
//
// Chunking for large payloads (class lists, method dumps):
//
//   JS splits large result into N chunks and sends:
//     {"type":"chunk","id":N,"seq":0,"total":N,"data":"<base64>"}
//     …
//     {"type":"chunk","id":N,"seq":N-1,"total":N,"data":"<base64>"}
//
//   C++ accumulates and, when all chunks arrive, decodes and completes
//   the matching PendingCommand. The 'total' field is on every chunk so
//   the receiver can start accumulating before it sees the first one.
//
// Threading model:
//
//   The router has two entry points:
//     • handle_message  — called from the JS thread by the script's
//                         message handler. Single-writer on the
//                         pending map (the JS thread is the only one
//                         that can trigger completions).
//     • send_command    — called from any thread (worker, UI). Blocks
//                         until the reply arrives, unless called from
//                         the JS thread itself — in which case it
//                         cannot block, and must be replaced by
//                         post_command (async) plus a callback.
//
//   This asymmetry is deliberate. Blocking the JS thread on a reply
//   from the JS thread is a guaranteed deadlock.
#ifndef YAMGG_CORE_MESSAGE_ROUTER_H
#define YAMGG_CORE_MESSAGE_ROUTER_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <json.hpp>

struct _GBytes;
typedef struct _GBytes GBytes;

namespace yamgg {

class MessageRouter {
public:
    using EventCallback   = std::function<void(const nlohmann::json&)>;
    using ReplyCallback   = std::function<void(bool ok, const nlohmann::json&)>;

    static MessageRouter& instance();

    // Called by the script's message handler when JS sends a message.
    // Thread-safe; only expected to be called from the JS thread.
    void handle_message(const char* json, GBytes* data);

    // ── Synchronous send ────────────────────────────────────
    // Posts a command and blocks until the reply arrives. MUST NOT be
    // called from the JS thread. Returns true and fills result on
    // success; returns false and fills error on failure.
    bool send_command(const std::string& op,
                      const nlohmann::json& args,
                      nlohmann::json& result,
                      std::string& error,
                      int timeout_ms = 5000);

    // ── Asynchronous send ───────────────────────────────────
    // Posts a command and returns immediately. The callback fires from
    // the JS thread when the reply arrives — callers that need to
    // touch the UI must marshal back via thread_sync::UiThreadQueue.
    void post_command(const std::string& op,
                      const nlohmann::json& args,
                      ReplyCallback callback);

    // ── Event subscription ──────────────────────────────────
    // Events are messages with "type":"event". Subscribers receive the
    // whole payload as JSON. Thread-safe.
    void subscribe(const std::string& name, EventCallback cb);
    void unsubscribe(const std::string& name);

    // Fires the callback on the JS thread whenever a log message arrives.
    void set_log_callback(std::function<void(const std::string& level,
                                              const std::string& message)> cb);

    // Subscribes to the "capabilities" event and routes it into
    // art::ArtGuard. Called once by bootstrap_start.
    void install_capabilities_subscription();

private:
    MessageRouter() = default;

    // Called from handle_message under the pending mutex.
    void try_deliver_reply(uint64_t id, bool ok,
                           const nlohmann::json& result,
                           const std::string& error);

    struct PendingSync {
        std::mutex        mu;
        std::condition_variable cv;
        bool              done = false;
        bool              ok   = false;
        nlohmann::json    result;
        std::string       error;
    };

    struct PendingAsync {
        ReplyCallback cb;
    };

    struct ChunkBuffer {
        uint64_t                 total    = 0;
        std::vector<std::string> parts;
        size_t                   received = 0;
    };

    std::mutex                                       pending_mu_;
    std::map<uint64_t, std::shared_ptr<PendingSync>> pending_sync_;
    std::map<uint64_t, PendingAsync>                 pending_async_;
    std::map<uint64_t, ChunkBuffer>                  chunks_;
    std::atomic<uint64_t>                            next_id_{1};

    std::mutex                                       subs_mu_;
    std::map<std::string, std::vector<EventCallback>> subscribers_;

    std::mutex                                       log_mu_;
    std::function<void(const std::string&, const std::string&)> log_cb_;
};

} // namespace yamgg

#endif // YAMGG_CORE_MESSAGE_ROUTER_H
