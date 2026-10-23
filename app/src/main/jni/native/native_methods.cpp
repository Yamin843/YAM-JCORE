// native/native_methods.cpp
#include "native/native_methods.h"

#include <android/log.h>

#include <climits>
#include <cstring>
#include <string>
#include <vector>

#include "art/art_guard.h"
#include "core/bootstrap.h"
#include "core/thread_sync.h"
#include "native/runtime.h"
#include "ui/main_window.h"
#include "ui/renderer.h"

namespace yamgg {
namespace native {

namespace {
constexpr const char* kTag = "YAMGG";

// ── Renderer passthroughs ──────────────────────────────────

void jni_on_surface_created(JNIEnv*, jclass) {
    yamgg_renderer_on_surface_created();
}

void jni_on_surface_changed(JNIEnv*, jclass, jint w, jint h) {
    yamgg_renderer_on_surface_changed((int)w, (int)h);
}

void jni_on_draw_frame(JNIEnv*, jclass, jint w, jint h) {
    yamgg_renderer_on_draw_frame((int)w, (int)h);
}

void jni_on_touch(JNIEnv*, jclass, jint action, jfloat x, jfloat y, jint pid) {
    yamgg_renderer_on_touch((int)action, (float)x, (float)y, (int)pid);
}

void jni_on_key(JNIEnv*, jclass, jint key_code, jint action) {
    yamgg_renderer_on_key((int)key_code, (int)action);
}

void jni_on_char(JNIEnv*, jclass, jint cp) {
    yamgg_renderer_on_char((int)cp);
}

void jni_on_scroll(JNIEnv*, jclass, jfloat dx, jfloat dy) {
    yamgg_renderer_on_scroll((float)dx, (float)dy);
}

jboolean jni_want_capture_mouse(JNIEnv*, jclass) {
    return yamgg_renderer_want_capture_mouse() ? JNI_TRUE : JNI_FALSE;
}

jboolean jni_want_text_input(JNIEnv*, jclass) {
    return yamgg_renderer_want_text_input() ? JNI_TRUE : JNI_FALSE;
}

void jni_set_density(JNIEnv*, jclass, jfloat density) {
    yamgg_renderer_set_density((float)density);
}

// ── Window geometry helpers ────────────────────────────────

jboolean jni_hit_test(JNIEnv*, jclass, jfloat x, jfloat y) {
    return MainWindow::instance().hit_test((float)x, (float)y)
        ? JNI_TRUE : JNI_FALSE;
}

jboolean jni_is_on_title_bar(JNIEnv*, jclass, jfloat y) {
    return MainWindow::instance().is_on_title_bar((float)y)
        ? JNI_TRUE : JNI_FALSE;
}

// ── Session control ────────────────────────────────────────

jstring jni_get_session_state(JNIEnv* env, jclass) {
    const char* name = "unknown";
    switch (current_state()) {
        case SessionState::Created:      name = "created";      break;
        case SessionState::Initialising: name = "initialising"; break;
        case SessionState::Ready:        name = "ready";        break;
        case SessionState::Running:      name = "running";      break;
        case SessionState::Shutting:     name = "shutting";     break;
        case SessionState::Dead:         name = "dead";         break;
    }
    return env->NewStringUTF(name);
}

jboolean jni_is_bridge_available(JNIEnv*, jclass) {
    return art::ArtGuard::instance().is_bridge_available() ? JNI_TRUE : JNI_FALSE;
}

jstring jni_get_bridge_info(JNIEnv* env, jclass) {
    const BridgeInfo& bi = bridge_info();
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"reported\":%s,\"available\":%s,\"variant\":\"%s\",\"api\":%d}",
        bi.reported ? "true" : "false",
        bi.available ? "true" : "false",
        bi.variant.c_str(),
        bi.apiLevel);
    return env->NewStringUTF(buf);
}

// ── Clipboard (via Android ClipboardManager) ───────────────

void jni_copy_to_clipboard(JNIEnv* env, jclass, jstring text) {
    if (text == nullptr) return;
    ThreadAttachScope scope;
    JNIEnv* e = scope.env();
    if (e == nullptr) return;

    // Get the Application context via ActivityThread.
    jclass at_cls = e->FindClass("android/app/ActivityThread");
    jmethodID current_at = e->GetStaticMethodID(at_cls,
        "currentActivityThread", "()Landroid/app/ActivityThread;");
    jobject at = e->CallStaticObjectMethod(at_cls, current_at);
    jmethodID get_app = e->GetMethodID(at_cls, "getApplication",
        "()Landroid/app/Application;");
    jobject app = e->CallObjectMethod(at, get_app);

    if (app == nullptr) {
        e->DeleteLocalRef(at_cls);
        e->DeleteLocalRef(at);
        return;
    }

    // Get ClipboardManager.
    jclass ctx_cls = e->FindClass("android/content/Context");
    jmethodID get_service = e->GetMethodID(ctx_cls, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring svc_name = e->NewStringUTF("clipboard");
    jobject cm = e->CallObjectMethod(app, get_service, svc_name);
    e->DeleteLocalRef(svc_name);

    if (cm != nullptr) {
        jclass cm_cls = e->GetObjectClass(cm);
        jmethodID set_text = e->GetMethodID(cm_cls, "setText",
            "(Ljava/lang/CharSequence;)V");
        e->CallVoidMethod(cm, set_text, text);
        e->DeleteLocalRef(cm_cls);
        e->DeleteLocalRef(cm);
    }

    e->DeleteLocalRef(ctx_cls);
    e->DeleteLocalRef(app);
    e->DeleteLocalRef(at);
    e->DeleteLocalRef(at_cls);
}

} // namespace

bool register_mod_view_natives(JNIEnv* env, jclass mod_view_class) {
    if (env == nullptr || mod_view_class == nullptr) return false;

    static const JNINativeMethod methods[] = {
        { "nativeOnSurfaceCreated", "()V",                    (void*)jni_on_surface_created },
        { "nativeOnSurfaceChanged", "(II)V",                  (void*)jni_on_surface_changed },
        { "nativeOnDrawFrame",      "(II)V",                  (void*)jni_on_draw_frame },
        { "nativeOnTouch",          "(IFFI)V",                (void*)jni_on_touch },
        { "nativeOnKey",            "(II)V",                  (void*)jni_on_key },
        { "nativeOnChar",           "(I)V",                   (void*)jni_on_char },
        { "nativeOnScroll",         "(FF)V",                  (void*)jni_on_scroll },
        { "nativeWantCaptureMouse", "()Z",                    (void*)jni_want_capture_mouse },
        { "nativeWantTextInput",    "()Z",                    (void*)jni_want_text_input },
        { "nativeSetDensity",       "(F)V",                   (void*)jni_set_density },
        { "nativeHitTest",          "(FF)Z",                  (void*)jni_hit_test },
        { "nativeIsTitleBar",       "(F)Z",                   (void*)jni_is_on_title_bar },
        { "nativeGetSessionState",  "()Ljava/lang/String;",   (void*)jni_get_session_state },
        { "nativeIsBridgeAvailable","()Z",                    (void*)jni_is_bridge_available },
        { "nativeGetBridgeInfo",    "()Ljava/lang/String;",   (void*)jni_get_bridge_info },
        { "nativeCopyToClipboard",  "(Ljava/lang/String;)V",  (void*)jni_copy_to_clipboard },
    };

    jint n = (jint)(sizeof(methods) / sizeof(methods[0]));
    if (env->RegisterNatives(mod_view_class, methods, n) != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "RegisterNatives failed");
        return false;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "RegisterNatives OK (%d methods)", (int)n);
    return true;
}

} // namespace native
} // namespace yamgg
