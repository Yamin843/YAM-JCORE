// native/dex_loader.h — load the embedded DEX via InMemoryDexClassLoader.
#ifndef YAMGG_NATIVE_DEX_LOADER_H
#define YAMGG_NATIVE_DEX_LOADER_H

#include <jni.h>

#include <string>

namespace yamgg {
namespace native {

class DexLoader {
public:
    static DexLoader& instance();

    // Load the DEX bytes stored in Generated/embedded_dex.h. Idempotent.
    // Returns true on success. Uses InMemoryDexClassLoader (API 26+).
    bool load(JNIEnv* env);

    // Look up a class by name through the DEX class loader. Returns a
    // local reference or nullptr.
    jclass find_class(JNIEnv* env, const char* name);

    // Global reference to the ClassLoader that loaded the DEX.
    jobject class_loader(JNIEnv* env) const;

    // Release references. Called from JNI_OnUnload.
    void detach(JNIEnv* env);

private:
    DexLoader() = default;

    jobject      class_loader_       = nullptr;  // global ref
    jclass       class_loader_class_ = nullptr;  // global ref
    bool         loaded_             = false;
};

} // namespace native
} // namespace yamgg

#endif // YAMGG_NATIVE_DEX_LOADER_H
