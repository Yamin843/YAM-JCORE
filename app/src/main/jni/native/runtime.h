// native/runtime.h — thin wrapper over JavaVM.
//
// Owns the JavaVM pointer captured at JNI_OnLoad. Provides attach/detach
// for threads that need a JNIEnv (JS thread, worker threads spawned by
// CallPanelTab, etc). JNIEnv is never cached: it's per-thread.
#ifndef YAMGG_NATIVE_RUNTIME_H
#define YAMGG_NATIVE_RUNTIME_H

#include <jni.h>

namespace yamgg {
namespace native {

class Runtime {
public:
    static Runtime& instance();

    bool initialize(JavaVM* vm);
    void shutdown();

    bool is_initialized() const;

    // Returns the JNIEnv for the calling thread, attaching if needed.
    // Returns nullptr on failure.
    JNIEnv* get_env();

    // Attach/detach the calling thread. Safe to call when already
    // attached; in that case `attached_now` is set to false.
    bool attach(JNIEnv** env, bool* attached_now);
    void detach();

    JavaVM* vm() const;

private:
    Runtime() = default;

    JavaVM*            vm_            = nullptr;
    bool               initialized_   = false;
};

// Convenience: RAII attach that detaches on scope exit only if it was
// this scope that attached.
class ThreadAttachScope {
public:
    ThreadAttachScope();
    ~ThreadAttachScope();

    JNIEnv* env() const;

private:
    JNIEnv* env_          = nullptr;
    bool    attached_now_ = false;
};

} // namespace native
} // namespace yamgg

#endif // YAMGG_NATIVE_RUNTIME_H
