// main.cpp — JNI entry point.
//
// Sequence:
//   1. JNI_OnLoad is invoked by the JVM (via System.loadLibrary or by
//      the injector). We capture the JavaVM in native::Runtime, start
//      the LIBYAMJS session (bootstrap_start), then start the wiring
//      worker that loads the DEX and attaches ModView.
//   2. JNI_OnUnload reverses the process: stop wiring, then stop the
//      LIBYAMJS session, then drop the JavaVM.
//
// The constructor / destructor log lines are useful for diagnosing
// load / unload ordering problems during injection.
#include <jni.h>
#include <android/log.h>

#include "core/bootstrap.h"
#include "native/runtime.h"
#include "native/wiring.h"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {

__attribute__((constructor))
static void yamgg_on_load(void) {
    LOGI("libyamgg.so loaded");
}

__attribute__((destructor))
static void yamgg_on_unload(void) {
    LOGI("libyamgg.so unloading");
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    LOGI("JNI_OnLoad");

    // ── 1. Capture the JavaVM before doing anything that might need it.
    if (!yamgg::native::Runtime::instance().initialize(vm)) {
        LOGE("Runtime::initialize failed");
        return JNI_ERR;
    }

    // ── 2. Start the LIBYAMJS session. This spins up the JS thread and
    // blocks until both prelude and bootstrap have reported ready.
    if (!yamgg::bootstrap_start(vm)) {
        LOGE("bootstrap_start failed (state=%d)",
             static_cast<int>(yamgg::current_state()));
        yamgg::native::Runtime::instance().shutdown();
        return JNI_ERR;
    }

    // ── 3. Start the wiring worker. It loads the DEX, registers the
    // ModView natives, and attaches ModView to whatever Activity is
    // currently resumed. Runs asynchronously.
    yamgg::native::start_wiring_thread();

    LOGI("JNI_OnLoad complete: session state=%d",
         static_cast<int>(yamgg::current_state()));
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNI_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
    LOGI("JNI_OnUnload");

    yamgg::native::stop_wiring_thread();
    yamgg::bootstrap_stop();
    yamgg::native::Runtime::instance().shutdown();
}

} // extern "C"
