// native/wiring.cpp
#include "native/wiring.h"

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "Generated/embedded_dex.h"
#include "native/dex_loader.h"
#include "native/java_bridge.h"
#include "native/native_methods.h"
#include "native/runtime.h"

#define LOG_TAG "YAMGG-Wiring"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace native {

namespace {

std::mutex               g_lifecycle_mu;
std::atomic<bool>        g_started{false};
std::atomic<bool>        g_stop_requested{false};
std::thread              g_worker;

// Global references held for the duration of the session. Released by
// stop_wiring_thread().
jclass  g_mod_view_class     = nullptr;
jclass  g_mod_view_helper    = nullptr;
jmethodID g_attach_method    = nullptr;
jmethodID g_helper_install   = nullptr;

// ── Locate the Application instance ─────────────────────────
// Uses ActivityThread.currentActivityThread().getApplication() which is
// the canonical way to reach it from native code without the
// deprecated ActivityThread.currentApplication().
jobject get_application(JNIEnv* env) {
    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (at_cls == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jmethodID current_at = env->GetStaticMethodID(at_cls,
        "currentActivityThread", "()Landroid/app/ActivityThread;");
    if (current_at == nullptr) {
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }

    jobject at = env->CallStaticObjectMethod(at_cls, current_at);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }
    if (at == nullptr) {
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }

    jmethodID get_app = env->GetMethodID(at_cls, "getApplication",
        "()Landroid/app/Application;");
    if (get_app == nullptr) {
        env->DeleteLocalRef(at);
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }

    jobject app = env->CallObjectMethod(at, get_app);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(at);
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }

    env->DeleteLocalRef(at);
    env->DeleteLocalRef(at_cls);
    return app;
}

// ── Find the first resumed Activity ────────────────────────
// Iterates ActivityThread.mActivities (an ArrayMap<IBinder,
// ActivityClientRecord>) and returns the first Activity that is not
// finishing. Returns nullptr if none is available yet.
jobject get_current_activity(JNIEnv* env) {
    jclass at_cls = env->FindClass("android/app/ActivityThread");
    if (at_cls == nullptr) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }

    jmethodID current_at = env->GetStaticMethodID(at_cls,
        "currentActivityThread", "()Landroid/app/ActivityThread;");
    jobject at = env->CallStaticObjectMethod(at_cls, current_at);
    if (env->ExceptionCheck() || at == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }

    jfieldID activities_field = env->GetFieldID(at_cls, "mActivities",
        "Landroid/util/ArrayMap;");
    if (activities_field == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(at);
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }

    jobject activities = env->GetObjectField(at, activities_field);
    if (env->ExceptionCheck() || activities == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(at);
        env->DeleteLocalRef(at_cls);
        return nullptr;
    }

    jclass am_cls = env->FindClass("android/util/ArrayMap");
    jmethodID size_m    = env->GetMethodID(am_cls, "size",    "()I");
    jmethodID value_at  = env->GetMethodID(am_cls, "valueAt", "(I)Ljava/lang/Object;");
    jint n = env->CallIntMethod(activities, size_m);

    jobject result = nullptr;
    for (jint i = 0; i < n; i++) {
        jobject record = env->CallObjectMethod(activities, value_at, i);
        if (env->ExceptionCheck() || record == nullptr) {
            env->ExceptionClear();
            continue;
        }

        jclass record_cls = env->GetObjectClass(record);
        jfieldID activity_field = env->GetFieldID(record_cls, "activity",
            "Landroid/app/Activity;");
        if (activity_field == nullptr) {
            env->ExceptionClear();
            env->DeleteLocalRef(record_cls);
            env->DeleteLocalRef(record);
            continue;
        }

        jobject activity = env->GetObjectField(record, activity_field);
        env->DeleteLocalRef(record_cls);
        env->DeleteLocalRef(record);

        if (activity == nullptr) continue;

        // Skip activities that are finishing or destroyed.
        jclass act_cls = env->FindClass("android/app/Activity");
        jmethodID is_finishing = env->GetMethodID(act_cls, "isFinishing", "()Z");
        jmethodID is_destroyed = env->GetMethodID(act_cls, "isDestroyed", "()Z");
        jboolean finish   = env->CallBooleanMethod(activity, is_finishing);
        jboolean destroyed = env->CallBooleanMethod(activity, is_destroyed);
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(act_cls);

        if (!finish && !destroyed) {
            result = activity;
            break;
        }
        env->DeleteLocalRef(activity);
    }

    env->DeleteLocalRef(am_cls);
    env->DeleteLocalRef(activities);
    env->DeleteLocalRef(at);
    env->DeleteLocalRef(at_cls);
    return result;
}

// ── Attach ModView to the current Activity ─────────────────
void attach_to_current_activity(JNIEnv* env) {
    jobject activity = get_current_activity(env);
    if (activity == nullptr) return;

    if (g_attach_method != nullptr && g_mod_view_class != nullptr) {
        env->CallStaticVoidMethod(g_mod_view_class, g_attach_method, activity);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        } else {
            LOGI("attached to existing Activity");
        }
    }

    env->DeleteLocalRef(activity);
}

// ── Worker main ────────────────────────────────────────────
void worker_main() {
    JNIEnv* env = nullptr;
    bool    attached = false;

    if (!Runtime::instance().attach(&env, &attached) || env == nullptr) {
        LOGE("failed to attach to JVM");
        return;
    }

    // Initialize the direct C++ → Java bridge.
    JavaBridge::instance().init(Runtime::instance().vm());

    // ── Step 1: refuse to proceed if the DEX was never generated.
    if (yamgg::kDexSize == 0) {
        LOGE("embedded DEX is empty; run scripts/build_dex.sh to generate it");
        if (attached) Runtime::instance().detach();
        return;
    }

    // ── Step 2: load the DEX.
    if (!DexLoader::instance().load(env)) {
        LOGE("failed to load DEX");
        if (attached) Runtime::instance().detach();
        return;
    }

    // ── Step 3: find ModView + ModViewHelper through the DEX loader.
    jclass mod_view = DexLoader::instance().find_class(
        env, "com.yamgg.modview.ModView");
    if (mod_view == nullptr) {
        LOGE("ModView class not found in DEX");
        if (attached) Runtime::instance().detach();
        return;
    }

    jclass mod_view_helper = DexLoader::instance().find_class(
        env, "com.yamgg.modview.ModViewHelper");
    if (mod_view_helper == nullptr) {
        LOGE("ModViewHelper class not found in DEX");
        env->DeleteLocalRef(mod_view);
        if (attached) Runtime::instance().detach();
        return;
    }

    // ── Step 4: register natives on ModView. This MUST happen before
    // any code calls a native method, so it is the first thing we do
    // after the class is resolved.
    if (!register_mod_view_natives(env, mod_view)) {
        LOGE("RegisterNatives failed");
        env->DeleteLocalRef(mod_view);
        env->DeleteLocalRef(mod_view_helper);
        if (attached) Runtime::instance().detach();
        return;
    }

    // ── Step 5: cache the methods we call from native code.
    g_mod_view_class  = static_cast<jclass>(env->NewGlobalRef(mod_view));
    g_mod_view_helper = static_cast<jclass>(env->NewGlobalRef(mod_view_helper));
    g_attach_method   = env->GetStaticMethodID(mod_view,
        "attach", "(Landroid/app/Activity;)V");
    g_helper_install  = env->GetStaticMethodID(mod_view_helper,
        "install", "(Landroid/app/Application;)V");

    env->DeleteLocalRef(mod_view);
    env->DeleteLocalRef(mod_view_helper);

    if (g_attach_method == nullptr || g_helper_install == nullptr) {
        LOGE("required methods missing on ModView / ModViewHelper");
        if (attached) Runtime::instance().detach();
        return;
    }

    // ── Step 6: wait until the Application object is available.
    // currentActivityThread() returns null before Application creation
    // finishes. We poll every 100 ms, with no upper bound — the caller
    // can request stop via g_stop_requested.
    jobject application = nullptr;
    while (!g_stop_requested.load(std::memory_order_acquire)) {
        application = get_application(env);
        if (application != nullptr) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (application == nullptr) {
        LOGI("stop requested before Application became available");
        if (attached) Runtime::instance().detach();
        return;
    }

    // ── Step 7: install lifecycle callbacks.
    env->CallStaticVoidMethod(g_mod_view_helper, g_helper_install, application);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGE("ModViewHelper.install threw");
    } else {
        LOGI("lifecycle callbacks installed");
    }

    // ── Step 8: attach to whatever Activity is already resumed, if any.
    // After this, subsequent Activity resumes are handled by the
    // lifecycle callbacks.
    attach_to_current_activity(env);

    env->DeleteLocalRef(application);

    // ── Step 9: keep the worker alive until stop is requested.
    // It has nothing else to do; it just waits so that the JNIEnv
    // remains attached and the global references stay alive. If this
    // becomes a concern for thread count, we can detach and reattach
    // on stop instead. For now the simplicity is worth it.
    while (!g_stop_requested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    LOGI("worker stop requested");
    if (attached) Runtime::instance().detach();
}

} // namespace

// ═══════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════

void start_wiring_thread() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) return;
    g_stop_requested.store(false, std::memory_order_release);
    g_worker = std::thread(worker_main);
    LOGI("wiring worker started");
}

void stop_wiring_thread() {
    if (!g_started.load(std::memory_order_acquire)) return;

    g_stop_requested.store(true, std::memory_order_release);

    if (g_worker.joinable()) {
        g_worker.join();
    }

    // Release cached globals.
    JNIEnv* env = Runtime::instance().get_env();
    if (env != nullptr) {
        if (g_mod_view_class  != nullptr) { env->DeleteGlobalRef(g_mod_view_class);  g_mod_view_class  = nullptr; }
        if (g_mod_view_helper != nullptr) { env->DeleteGlobalRef(g_mod_view_helper); g_mod_view_helper = nullptr; }
    }
    g_attach_method  = nullptr;
    g_helper_install = nullptr;

    g_started.store(false, std::memory_order_release);
    LOGI("wiring worker stopped");
}

} // namespace native
} // namespace yamgg
