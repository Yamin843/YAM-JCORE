// native/wiring.h — glue between the native library and the Java
// classes embedded in the DEX.
//
// Responsibilities:
//   * Load the embedded DEX through InMemoryDexClassLoader.
//   * Register the JNINativeMethod table on ModView.
//   * Install ModViewHelper.Application.ActivityLifecycleCallbacks so
//     ModView attaches to whichever Activity is currently resumed.
//   * If an Activity is already resumed at install time, attach to it
//     immediately.
//
// All of the above run on a dedicated worker thread started once from
// JNI_OnLoad. The worker attaches itself to the JVM, does its work,
// then detaches. It has no timeout: it polls until the DEX is loaded
// and the Application is available, then attaches. If the DEX fails to
// load (kDexSize == 0, meaning build_dex.sh has not been run yet), the
// worker stops with a logged reason and does not retry.
#ifndef YAMGG_NATIVE_WIRING_H
#define YAMGG_NATIVE_WIRING_H

namespace yamgg {
namespace native {

// Start the wiring worker. Idempotent — subsequent calls are no-ops.
// The worker runs detached from the caller's thread.
void start_wiring_thread();

// Stop the wiring worker and release any resources it holds. Called
// from JNI_OnUnload. Idempotent.
void stop_wiring_thread();

} // namespace native
} // namespace yamgg

#endif // YAMGG_NATIVE_WIRING_H
