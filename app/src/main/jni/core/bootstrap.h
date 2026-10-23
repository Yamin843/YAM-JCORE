// core/bootstrap.h — session lifecycle orchestrator.
//
// Responsibilities:
//   * Initialise the embedded gum runtime (yam_init_embedded).
//   * Spawn the JS thread via g_thread_new (NOT pthread_create),
//     so that gum_on_thread_realize registers the correct
//     ThreadIgnoreScope and Cloak entries.
//   * Obtain the JS scheduler and its dedicated GMainContext,
//     disable the background scheduler thread, and run our own
//     main loop on that context.
//   * Load the prelude + bootstrap scripts and wait until both
//     report "ready".
//   * Expose the state to the rest of the project.
//
// The lifecycle is:
//   Created → Initialising → Ready → (Running) → Shutting → Dead
//
// Everything that runs before Ready is synchronous and blocking
// on the caller's thread (JNI_OnLoad). Everything after Ready is
// asynchronous; UI calls go through message_router.
#ifndef YAMGG_CORE_BOOTSTRAP_H
#define YAMGG_CORE_BOOTSTRAP_H

#include <jni.h>
#include <atomic>

// YAMJS.h exposes the renamed Gum + GumJS API (yam_* instead of gum_*).
// It also transitively includes GLib (g_thread_new, GMainLoop, GBytes, …),
// which is bundled inside libyamjs.a and resolved at link time.
#include "YAMJS.h"

namespace yamgg {

// Lifecycle states visible to the rest of the project.
enum class SessionState : int {
    Created      = 0,
    Initialising = 1,
    Ready        = 2,
    Running      = 3,
    Shutting     = 4,
    Dead         = 5,
};

// Summary of what the prelude reported about the environment.
// Populated during initialisation and read-only thereafter.
struct BridgeInfo {
    bool         reported       = false;
    bool         hasJava        = false;
    bool         available      = false;
    std::string  variant;                    // "official" | "notarthook" | ""
    int          apiLevel       = 0;         // build.version.sdk
};

// Starts the session. Must be called exactly once, from JNI_OnLoad,
// on the Android main thread. Blocks until the JS side has reported
// "ready" from both prelude and bootstrap, or until init fails.
//
// Returns true on success. On failure, the session is left in
// SessionState::Dead and currentState() will report it.
bool bootstrap_start(JavaVM* vm);

// Stops the session: quits the JS main loop, joins the JS thread,
// unloads the scripts, and releases the runtime.
//
// Safe to call from JNI_OnUnload after bootstrap_start has returned.
// Safe to call multiple times; subsequent calls are no-ops.
void bootstrap_stop();

// State queries, thread-safe.
SessionState  current_state();
const BridgeInfo& bridge_info();

// Convenience: true once the session is at least Running.
bool is_ready();

// Accessor for downstream layers.
YamScriptBackend* backend();
YamScript*        prelude_script();
YamScript*        bootstrap_script();
YamScheduler*     scheduler();
GMainContext*     js_context();

// Called by the message router when it has fully processed a "ready"
// message. Wakes bootstrap_start's wait.
void bootstrap_signal_ready(const char* phase);

// Called by the message router when it has fully processed an "error"
// message during initialisation. Marks the session Dead and wakes
// bootstrap_start's wait.
void bootstrap_signal_fatal(const char* message);

} // namespace yamgg

#endif // YAMGG_CORE_BOOTSTRAP_H
