// native/dex_loader.cpp
#include "native/dex_loader.h"

#include <android/log.h>

#include <mutex>

#include "Generated/embedded_dex.h"

namespace yamgg {
namespace native {

namespace {
std::mutex g_mu;
constexpr const char* kTag = "YAMGG";
}

DexLoader& DexLoader::instance() {
    static DexLoader l;
    return l;
}

bool DexLoader::load(JNIEnv* env) {
    if (env == nullptr) return false;

    std::lock_guard<std::mutex> lk(g_mu);
    if (loaded_) return true;

    // Class-loading machinery.
    jclass imdcl = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (imdcl == nullptr) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "InMemoryDexClassLoader unavailable (requires API 26+)");
        return false;
    }

    jclass bb_class = env->FindClass("java/nio/ByteBuffer");
    jmethodID bb_wrap = env->GetStaticMethodID(bb_class, "wrap",
        "([B)Ljava/nio/ByteBuffer;");

    jbyteArray arr = env->NewByteArray((jsize)yamgg::kDexSize);
    env->SetByteArrayRegion(arr, 0, (jsize)yamgg::kDexSize,
        reinterpret_cast<const jbyte*>(yamgg::kDexBytes));
    jobject buf = env->CallStaticObjectMethod(bb_class, bb_wrap, arr);
    env->DeleteLocalRef(arr);

    jclass cl_class = env->FindClass("java/lang/ClassLoader");
    jmethodID ctor = env->GetMethodID(imdcl, "<init>",
        "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");

    jobject loader = env->NewObject(imdcl, ctor, buf, nullptr);
    env->DeleteLocalRef(buf);

    if (env->ExceptionCheck() || loader == nullptr) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "InMemoryDexClassLoader construction failed");
        return false;
    }

    class_loader_       = env->NewGlobalRef(loader);
    class_loader_class_ = static_cast<jclass>(env->NewGlobalRef(cl_class));
    loaded_             = true;

    env->DeleteLocalRef(loader);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "DEX loaded (%u bytes)", (unsigned)yamgg::kDexSize);
    return true;
}

jclass DexLoader::find_class(JNIEnv* env, const char* name) {
    if (env == nullptr || name == nullptr) return nullptr;
    std::lock_guard<std::mutex> lk(g_mu);
    if (class_loader_ != nullptr && class_loader_class_ != nullptr) {
        jmethodID load_class = env->GetMethodID(class_loader_class_,
            "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if (load_class != nullptr) {
            jstring jname = env->NewStringUTF(name);
            auto cls = static_cast<jclass>(
                env->CallObjectMethod(class_loader_, load_class, jname));
            env->DeleteLocalRef(jname);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return nullptr;
            }
            return cls;
        }
    }
    // Fallback: system loader.
    jclass cls = env->FindClass(name);
    if (cls == nullptr && env->ExceptionCheck()) env->ExceptionClear();
    return cls;
}

jobject DexLoader::class_loader(JNIEnv* env) const {
    (void)env;
    std::lock_guard<std::mutex> lk(g_mu);
    return class_loader_;
}

void DexLoader::detach(JNIEnv* env) {
    if (env == nullptr) return;
    std::lock_guard<std::mutex> lk(g_mu);
    if (class_loader_ != nullptr) {
        env->DeleteGlobalRef(class_loader_);
        class_loader_ = nullptr;
    }
    if (class_loader_class_ != nullptr) {
        env->DeleteGlobalRef(class_loader_class_);
        class_loader_class_ = nullptr;
    }
    loaded_ = false;
}

} // namespace native
} // namespace yamgg
