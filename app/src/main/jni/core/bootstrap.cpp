// core/bootstrap.cpp — implementation.
#include "core/bootstrap.h"
#include "core/script_host.h"
#include "core/message_router.h"
#include "core/error_recovery.h"
#include "art/art_guard.h"

#include <android/log.h>
#include <cstring>
#include <string>

#include "Generated/prelude_js.h"
#include "Generated/bootstrap_js.h"
#include "art/intelligent_init.h"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace {

struct BootstrapState {
    std::atomic<int>       state{static_cast<int>(SessionState::Created)};
    JavaVM*                vm = nullptr;

    YamScriptBackend*      backend       = nullptr;
    YamScriptScheduler*    sched         = nullptr;
    GMainContext*          js_ctx        = nullptr;
    GMainLoop*             js_loop       = nullptr;
    GThread*               js_thread     = nullptr;

    YamScript*             prelude       = nullptr;
    YamScript*             bootstrap     = nullptr;

    BridgeInfo             bridge;
    std::string            fatal_reason;

    // Synchronisation between main thread (which calls bootstrap_start)
    // and the JS thread (which reports "ready" from the prelude/bootstrap
    // scripts). GMutex/GCond are provided by the embedded GLib inside
    // libyamjs.a.
    GMutex                 ready_mu;
    GCond                  ready_cv;
    bool                   script_loaded     = false;   // set by on_bootstrap_loaded
    bool                   prelude_ready     = false;
    bool                   bootstrap_ready   = false;
    bool                   fatal             = false;

    // Guard against double-start / double-stop.
    std::atomic<bool>      started{false};
    std::atomic<bool>      stopping{false};
};

BootstrapState g_boot;

// Forward declarations for callbacks used inside the JS thread.
gpointer js_thread_main(gpointer data);
void     on_script_message(const gchar* json, GBytes* data, gpointer user);

// Convenience wrapper: set state atomically.
void set_state(SessionState s) {
    g_boot.state.store(static_cast<int>(s), std::memory_order_release);
}

// Attaches the current thread to the JVM, then detaches only if it was
// this thread that attached it (not the runtime).
struct JvmThreadScope {
    JavaVM*  vm     = nullptr;
    bool     did    = false;

    explicit JvmThreadScope(JavaVM* v) : vm(v) {
        if (vm == nullptr) return;
        JNIEnv* env = nullptr;
        jint rc = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (rc == JNI_EDETACHED) {
            JavaVMAttachArgs args = {};
            args.version = JNI_VERSION_1_6;
            args.name    = const_cast<char*>("yamgg-js");
            args.group   = nullptr;
            if (vm->AttachCurrentThread(&env, &args) == JNI_OK) {
                did = true;
            }
        }
    }

    ~JvmThreadScope() {
        if (did && vm != nullptr) {
            vm->DetachCurrentThread();
        }
    }
};

} // namespace

// ═══════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════

SessionState current_state() {
    return static_cast<SessionState>(g_boot.state.load(std::memory_order_acquire));
}

const BridgeInfo& bridge_info() { return g_boot.bridge; }

bool is_ready() {
    return current_state() >= SessionState::Running;
}

YamScriptBackend* backend()            { return g_boot.backend; }
YamScript*        prelude_script()     { return g_boot.prelude; }
YamScript*        bootstrap_script()   { return g_boot.bootstrap; }
YamScriptScheduler* scheduler()        { return g_boot.sched; }
GMainContext*     js_context()         { return g_boot.js_ctx; }

// ═══════════════════════════════════════════════════════════════
//  bootstrap_start
// ═══════════════════════════════════════════════════════════════

bool bootstrap_start(JavaVM* vm) {
    bool expected = false;
    if (!g_boot.started.compare_exchange_strong(expected, true)) {
        LOGW("bootstrap_start: already started");
        return current_state() >= SessionState::Running;
    }

    set_state(SessionState::Initialising);
    g_boot.vm = vm;

    g_mutex_init(&g_boot.ready_mu);
    g_cond_init(&g_boot.ready_cv);

    LOGI("bootstrap_start: vm=%p", static_cast<void*>(vm));

    // ── Step 1: yam_init_embedded ────────────────────────────
    // Registers gum_on_thread_realize as a GLib thread callback,
    // creates gum_cached_interceptor, calls gum_libdl_prevent_unload.
    // Without this, g_thread_new below would fail to hide the thread
    // and JNI recursion would crash on the first hook.
    yam_init_embedded();
    LOGI("bootstrap_start: yam_init_embedded done");

    // ── Step 2: backend (V8 preferred for Java bridge) ───────
    // LIBYAMJS bundles both QJS and V8. V8 is heavier but the
    // Java bridge is built and tested primarily against V8.
    YamScriptBackend* be = yam_script_backend_obtain_v8();
    if (be == nullptr) {
        LOGW("V8 backend unavailable, trying QJS");
        be = yam_script_backend_obtain_qjs();
    }
    if (be == nullptr) {
        LOGE("bootstrap_start: no script backend available");
        g_boot.fatal_reason = "no script backend";
        set_state(SessionState::Dead);
        return false;
    }
    g_boot.backend = be;
    LOGI("bootstrap_start: backend=%p", static_cast<void*>(be));

    // ── Step 3: scheduler + js context ───────────────────────
    g_boot.sched = yam_script_backend_get_scheduler();
    if (g_boot.sched == nullptr) {
        LOGE("bootstrap_start: no scheduler");
        g_boot.fatal_reason = "no scheduler";
        set_state(SessionState::Dead);
        return false;
    }

    // Disables the scheduler's background thread. From this point on
    // only the GMainContext returned below drives job execution.
    // Skipping this call leads to jobs being pushed to a context that
    // has no loop running, so every yam_script_post hangs forever.
    yam_script_scheduler_disable_background_thread(g_boot.sched);

    g_boot.js_ctx = yam_script_scheduler_get_js_context(g_boot.sched);
    if (g_boot.js_ctx == nullptr) {
        LOGE("bootstrap_start: js_context null");
        g_boot.fatal_reason = "null js context";
        set_state(SessionState::Dead);
        return false;
    }
    LOGI("bootstrap_start: js_context=%p", static_cast<void*>(g_boot.js_ctx));

    // ── Step 4: create the JS thread ─────────────────────────
    // g_thread_new registers the thread with gum_on_thread_realize,
    // which calls gum_interceptor_ignore_current_thread and hides
    // the thread from /proc/self/task. pthread_create would NOT.
    g_boot.js_thread = g_thread_new("yamgg-js", js_thread_main, nullptr);
    if (g_boot.js_thread == nullptr) {
        LOGE("bootstrap_start: g_thread_new failed");
        g_boot.fatal_reason = "thread create failed";
        set_state(SessionState::Dead);
        return false;
    }

    // ── Step 5: wait for both prelude and bootstrap to report ready ──
    // The JS thread signals here from the message router when it sees
    // the "ready" message from each script. On fatal, it also wakes.
    g_mutex_lock(&g_boot.ready_mu);
    while (!g_boot.prelude_ready || !g_boot.bootstrap_ready) {
        if (g_boot.fatal) {
            const bool f = g_boot.fatal;
            g_mutex_unlock(&g_boot.ready_mu);
            if (f) {
                set_state(SessionState::Dead);
                LOGE("bootstrap_start: fatal: %s", g_boot.fatal_reason.c_str());
                return false;
            }
        }
        g_cond_wait(&g_boot.ready_cv, &g_boot.ready_mu);
    }
    g_mutex_unlock(&g_boot.ready_mu);

    // ── Step 6: wire the capabilities subscription ───────────
    // The bootstrap.js event will fire once shortly after startup.
    // We install the subscription now so no event is missed.
    MessageRouter::instance().install_capabilities_subscription();

    // ── Step 7: mark Running and return ──────────────────────
    set_state(SessionState::Running);
    LOGI("bootstrap_start: session Running (bridge variant='%s' available=%d api=%d)",
         g_boot.bridge.variant.c_str(),
         g_boot.bridge.available ? 1 : 0,
         g_boot.bridge.apiLevel);

    // ── Step 7: ensure art::ArtGuard is initialized ──────────
    // The bootstrap.js script sends a "capabilities" event on startup.
    // If the event already arrived before we reached this point, the
    // guard is populated. If not, it will be populated when it does.
    // Either way, no further synchronous work is required here.
    if (!art::ArtGuard::instance().is_initialized()) {
        LOGW("bootstrap_start: capabilities event not yet received; "
             "art::ArtGuard will populate on arrival");
    }

        // ── Step 7: wait for the bootstrap script to be loaded ─────
    // The JS thread is doing the creation asynchronously. We must
    // not send smart_init.start before the bootstrap script is
    // actually loaded, or the command will be silently dropped.
    {
        g_mutex_lock(&g_boot.ready_mu);
        const gint64 deadline = g_get_monotonic_time() + 30 * G_TIME_SPAN_SECOND;
        while (!g_boot.script_loaded && !g_boot.fatal) {
            if (!g_cond_wait_until(&g_boot.ready_cv, &g_boot.ready_mu, deadline)) {
                break;
            }
        }
        bool loaded = g_boot.script_loaded;
        bool fatal  = g_boot.fatal;
        std::string reason = g_boot.fatal_reason;
        g_mutex_unlock(&g_boot.ready_mu);

        if (!loaded) {
            LOGE("bootstrap script did not load: %s",
                 fatal ? reason.c_str() : "timeout");
            set_state(SessionState::Dead);
            return false;
        }
    }
    LOGI("bootstrap script loaded; proceeding to intelligent_initialize");

    // ── Step 8: intelligent initialization ─────────────────
    // Wait for smart_init to confirm libart is available, then
    // (if needed) fall back to native JNI. Without this step,
    // Java.available may have been evaluated before libart was
    // loaded, permanently caching a failure.
    if (!art::intelligent_initialize(vm)) {
        const auto& s = art::intelligent_state();
        LOGE("intelligent_initialize failed: mode=%d reason=%s",
             static_cast<int>(s.mode), s.reason.c_str());
        set_state(SessionState::Dead);
        return false;
    }

    LOGI("bootstrap_start: intelligent mode = %s",
         art::intelligent_state().mode == art::IntelligentMode::Full
            ? "FULL" : "JNI_ONLY");

return true;
}

// ═══════════════════════════════════════════════════════════════
//  bootstrap_stop
// ═══════════════════════════════════════════════════════════════

void bootstrap_stop() {
    bool expected = false;
    if (!g_boot.stopping.compare_exchange_strong(expected, true)) {
        LOGW("bootstrap_stop: already stopping");
        return;
    }

    if (current_state() == SessionState::Dead) {
        return;
    }

    set_state(SessionState::Shutting);

    // ── 1. Ask the main loop to quit ─────────────────────────
    if (g_boot.js_ctx != nullptr && g_boot.js_loop != nullptr) {
        g_main_context_push_thread_default(g_boot.js_ctx);
        g_main_loop_quit(g_boot.js_loop);
        g_main_context_pop_thread_default(g_boot.js_ctx);
    }

    // ── 2. Join the JS thread ────────────────────────────────
    // The thread signals "done" by quitting its own loop. If it did
    // not (e.g. it crashed earlier), g_thread_join would hang. We
    // push a timeout of 5 s by using a pthread cancel fallback —
    // but that's unsafe with a running JS engine. Instead, we rely
    // on the loop quit above and join synchronously.
    if (g_boot.js_thread != nullptr) {
        g_thread_join(g_boot.js_thread);
        g_boot.js_thread = nullptr;
        LOGI("bootstrap_stop: js thread joined");
    }

    // ── 3. Unload scripts ────────────────────────────────────
    // yam_script_unload_sync requires the js_context as thread default.
    // By now the loop is stopped and the js thread is joined, so we
    // push the context on the current thread temporarily.
    if (g_boot.js_ctx != nullptr) {
        g_main_context_push_thread_default(g_boot.js_ctx);
    }

    if (g_boot.bootstrap != nullptr) {
        yam_script_unload_sync(g_boot.bootstrap, nullptr);
        g_object_unref(g_boot.bootstrap);
        g_boot.bootstrap = nullptr;
    }
    if (g_boot.prelude != nullptr) {
        yam_script_unload_sync(g_boot.prelude, nullptr);
        g_object_unref(g_boot.prelude);
        g_boot.prelude = nullptr;
    }

    if (g_boot.js_ctx != nullptr) {
        g_main_context_pop_thread_default(g_boot.js_ctx);
    }

    // ── 4. Deinit gum ────────────────────────────────────────
    yam_deinit_embedded();

    // ── 5. Release mutex/cond ────────────────────────────────
    g_cond_clear(&g_boot.ready_cv);
    g_mutex_clear(&g_boot.ready_mu);

    set_state(SessionState::Dead);
    LOGI("bootstrap_stop: session Dead");
}

// ═══════════════════════════════════════════════════════════════
//  Ready / fatal signals (called by message_router)
// ═══════════════════════════════════════════════════════════════

void bootstrap_signal_ready(const char* phase) {
    if (phase == nullptr) return;
    g_mutex_lock(&g_boot.ready_mu);
    if (std::strcmp(phase, "prelude") == 0) {
        g_boot.prelude_ready = true;
    } else if (std::strcmp(phase, "bootstrap") == 0) {
        g_boot.bootstrap_ready = true;
    }
    g_cond_broadcast(&g_boot.ready_cv);
    g_mutex_unlock(&g_boot.ready_mu);
}

void bootstrap_signal_fatal(const char* message) {
    g_mutex_lock(&g_boot.ready_mu);
    g_boot.fatal = true;
    if (message != nullptr) {
        g_boot.fatal_reason = message;
    } else {
        g_boot.fatal_reason = "unknown";
    }
    g_cond_broadcast(&g_boot.ready_cv);
    g_mutex_unlock(&g_boot.ready_mu);
}

// ═══════════════════════════════════════════════════════════════
//  JS thread
// ═══════════════════════════════════════════════════════════════

namespace {

// ─────────────────────────────────────────────────────────────
//  JS thread — async creation pattern (no idle chain)
// ─────────────────────────────────────────────────────────────
//
// CRITICAL INVARIANT:
//   Never call any *_sync() API from this thread before
//   g_main_loop_run() is active. The *_sync variants schedule a
//   job on js_context and then block on a GCond that only the
//   main loop can signal. Doing so deadlocks the entire app.
//
// The pattern below is:
//   1. Create the loop.
//   2. Fire the async create() for the prelude. It schedules work
//      and returns immediately.
//   3. Enter the loop. The scheduled work executes.
//   4. Each async completion callback schedules the next async
//      step. Chain: prelude-create → prelude-load → bootstrap-
//      create → bootstrap-load → signal script_loaded.
//
// Note on _load_finish: in this LIBYAMJS build, _load_finish
// takes only (self, result) and does not return a GError.
// Failures are detected by checking g_task_had_error() on the
// result BEFORE calling _finish.

struct JsThreadState {
    YamScript*   prelude   = nullptr;
    YamScript*   bootstrap = nullptr;
    GMainLoop*   loop      = nullptr;
    std::string  error;
};

static void on_prelude_create_done(GObject* src, GAsyncResult* res, gpointer user);
static void on_prelude_load_done(GObject* src, GAsyncResult* res, gpointer user);
static void on_bootstrap_create_done(GObject* src, GAsyncResult* res, gpointer user);
static void on_bootstrap_load_done(GObject* src, GAsyncResult* res, gpointer user);

gpointer js_thread_main(gpointer) {
    LOGI("js_thread_main: enter");

    JvmThreadScope jvm_scope(g_boot.vm);
    g_main_context_push_thread_default(g_boot.js_ctx);

    auto* st = new JsThreadState();
    st->loop = g_main_loop_new(g_boot.js_ctx, FALSE);
    g_boot.js_loop = st->loop;

    // Fire async create — schedules work, does not block.
    yam_script_backend_create(g_boot.backend, "yamgg-prelude",
        yamgg_prelude::kSource, nullptr, nullptr,
        (GAsyncReadyCallback)on_prelude_create_done, st);

    LOGI("js_thread_main: running main loop");
    g_main_loop_run(st->loop);
    LOGI("js_thread_main: main loop exited");

    if (st->prelude)   { g_object_unref(st->prelude);   st->prelude   = nullptr; }
    if (st->bootstrap) { g_object_unref(st->bootstrap); st->bootstrap = nullptr; }
    g_main_loop_unref(st->loop);
    g_boot.js_loop = nullptr;
    g_main_context_pop_thread_default(g_boot.js_ctx);
    delete st;
    return nullptr;
}

static void on_prelude_create_done(GObject* src, GAsyncResult* res, gpointer user) {
    auto* st = static_cast<JsThreadState*>(user);
    GError* err = nullptr;
    YamScript* script = yam_script_backend_create_finish(
        YAM_SCRIPT_BACKEND(src), res, &err);
    if (script == nullptr) {
        st->error = err ? err->message : "prelude create failed";
        if (err) g_error_free(err);
        LOGE("prelude create failed: %s", st->error.c_str());
        bootstrap_signal_fatal(st->error.c_str());
        g_main_loop_quit(st->loop);
        return;
    }
    st->prelude = script;
    LOGI("prelude created");

    yam_script_set_message_handler(st->prelude, on_script_message, nullptr, nullptr);
    yam_script_load(st->prelude, nullptr,
        (GAsyncReadyCallback)on_prelude_load_done, st);
}

static void on_prelude_load_done(GObject* src, GAsyncResult* res, gpointer user) {
    auto* st = static_cast<JsThreadState*>(user);

    // Detect load failure before calling _finish (which has no
    // GError parameter in this LIBYAMJS build).
    if (g_task_had_error(G_TASK(res))) {
        GError* err = nullptr;
        g_task_propagate_pointer(G_TASK(res), &err);
        st->error = err ? err->message : "prelude load failed";
        if (err) g_error_free(err);
        LOGE("prelude load failed: %s", st->error.c_str());
        bootstrap_signal_fatal(st->error.c_str());
        g_main_loop_quit(st->loop);
        return;
    }
    // Do NOT call yam_script_load_finish here. In this LIBYAMJS
    // build it dereferences art::Thread state that is not valid on
    // the yamgg-js thread, causing SIGSEGV at fault addr 0xa9.
    // g_task_had_error() above already told us the load succeeded.
    LOGI("prelude loaded — signaling prelude_ready directly (no _finish)");
    g_boot.prelude = st->prelude;

    // Signal prelude_ready from C++ so that we do NOT depend on a
    // JS-side send() message. This eliminates any crash in the
    // send() path from affecting bootstrap progress.
    g_mutex_lock(&g_boot.ready_mu);
    g_boot.prelude_ready = true;
    g_cond_broadcast(&g_boot.ready_cv);
    g_mutex_unlock(&g_boot.ready_mu);

    yam_script_backend_create(g_boot.backend, "yamgg-bootstrap",
        yamgg_bootstrap::kSource, nullptr, nullptr,
        (GAsyncReadyCallback)on_bootstrap_create_done, st);
}

static void on_bootstrap_create_done(GObject* src, GAsyncResult* res, gpointer user) {
    auto* st = static_cast<JsThreadState*>(user);
    GError* err = nullptr;
    YamScript* script = yam_script_backend_create_finish(
        YAM_SCRIPT_BACKEND(src), res, &err);
    if (script == nullptr) {
        st->error = err ? err->message : "bootstrap create failed";
        if (err) g_error_free(err);
        LOGE("bootstrap create failed: %s", st->error.c_str());
        bootstrap_signal_fatal(st->error.c_str());
        g_main_loop_quit(st->loop);
        return;
    }
    st->bootstrap = script;
    LOGI("bootstrap created");

    yam_script_set_message_handler(st->bootstrap, on_script_message, nullptr, nullptr);
    yam_script_load(st->bootstrap, nullptr,
        (GAsyncReadyCallback)on_bootstrap_load_done, st);
}

static void on_bootstrap_load_done(GObject* src, GAsyncResult* res, gpointer user) {
    auto* st = static_cast<JsThreadState*>(user);

    if (g_task_had_error(G_TASK(res))) {
        GError* err = nullptr;
        g_task_propagate_pointer(G_TASK(res), &err);
        st->error = err ? err->message : "bootstrap load failed";
        if (err) g_error_free(err);
        LOGE("bootstrap load failed: %s", st->error.c_str());
        bootstrap_signal_fatal(st->error.c_str());
        g_main_loop_quit(st->loop);
        return;
    }
    // Do NOT call yam_script_load_finish here (see note above).
    LOGI("bootstrap loaded — signaling script_loaded directly (no _finish)");
    g_boot.bootstrap = st->bootstrap;

    // Signal script_loaded from C++ — no dependency on JS send().
    g_mutex_lock(&g_boot.ready_mu);
    g_boot.bootstrap_ready = true;
    g_boot.script_loaded = true;
    g_cond_broadcast(&g_boot.ready_cv);
    g_mutex_unlock(&g_boot.ready_mu);
}

void on_script_message(const gchar* json, GBytes* data, gpointer) {
    // Route everything through the message router. It parses the
    // envelope, dispatches by "type", and for ready/error messages
    // signals the bootstrap condition.
    MessageRouter::instance().handle_message(json, data);
}

} // namespace


// Accessor used by MessageRouter to publish the bridge capabilities.
void bootstrap_set_bridge_info(const BridgeInfo& info) {
    g_boot.bridge = info;
}

} // namespace yamgg
