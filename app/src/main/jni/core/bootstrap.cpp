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

gpointer js_thread_main(gpointer) {
    LOGI("js_thread_main: enter");

    // Attach this thread to the Java VM. The scope detaches on exit
    // only if we did the attach ourselves.
    JvmThreadScope jvm_scope(g_boot.vm);

    // Push the dedicated JS context as thread default. Without this,
    // g_idle_source_new / g_timeout_source_new created inside the
    // scheduler's push_job functions would go to the process default
    // context, which is never iterated by our loop below.
    g_main_context_push_thread_default(g_boot.js_ctx);

    // ── Load prelude ─────────────────────────────────────────
    {
        GError* err = nullptr;
        YamScript* script = yam_script_backend_create_sync(
            g_boot.backend,
            "yamgg-prelude",
            yamgg_prelude::kSource,
            nullptr,
            nullptr,
            &err);
        if (script == nullptr) {
            LOGE("prelude create failed: %s",
                 err != nullptr ? err->message : "unknown");
            if (err) g_error_free(err);
            bootstrap_signal_fatal("prelude create failed");
            g_main_context_pop_thread_default(g_boot.js_ctx);
            return nullptr;
        }
        yam_script_set_message_handler(script, on_script_message, nullptr, nullptr);
        yam_script_load_sync(script, nullptr);
        g_boot.prelude = script;
        LOGI("prelude loaded (%zu bytes)", std::strlen(yamgg_prelude::kSource));
    }

    // ── Load bootstrap ───────────────────────────────────────
    {
        GError* err = nullptr;
        YamScript* script = yam_script_backend_create_sync(
            g_boot.backend,
            "yamgg-bootstrap",
            yamgg_bootstrap::kSource,
            nullptr,
            nullptr,
            &err);
        if (script == nullptr) {
            LOGE("bootstrap create failed: %s",
                 err != nullptr ? err->message : "unknown");
            if (err) g_error_free(err);
            bootstrap_signal_fatal("bootstrap create failed");
            g_main_context_pop_thread_default(g_boot.js_ctx);
            return nullptr;
        }
        yam_script_set_message_handler(script, on_script_message, nullptr, nullptr);
        yam_script_load_sync(script, nullptr);
        g_boot.bootstrap = script;
        LOGI("bootstrap loaded (%zu bytes)", std::strlen(yamgg_bootstrap::kSource));
    }

    // ── Run main loop ────────────────────────────────────────
    // We create the GMainLoop here (not in bootstrap_start) so that
    // g_main_loop_quit in bootstrap_stop has a valid pointer to quit.
    g_boot.js_loop = g_main_loop_new(g_boot.js_ctx, FALSE);
    LOGI("js_thread_main: entering main loop");
    g_main_loop_run(g_boot.js_loop);
    LOGI("js_thread_main: main loop exited");

    // ── Cleanup on exit ──────────────────────────────────────
    g_main_loop_unref(g_boot.js_loop);
    g_boot.js_loop = nullptr;

    g_main_context_pop_thread_default(g_boot.js_ctx);
    return nullptr;
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
