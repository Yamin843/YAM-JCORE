// native/java_bridge.cpp
#include "native/java_bridge.h"

#include <android/log.h>

#include <mutex>
#include <string>
#include <unordered_map>

#define LOG_TAG "YAMGG-JBridge"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace native {

// ─── Per-thread JNIEnv cache ────────────────────────────────
namespace {
thread_local JNIEnv* g_tls_env = nullptr;
thread_local bool    g_tls_attached = false;
} // namespace

struct JavaBridge::ClassCache {
    std::mutex mu;
    std::unordered_map<std::string, jclass>    classes;
    std::unordered_map<std::string, jmethodID> methods;
    std::unordered_map<std::string, jfieldID>  fields;
};

JavaBridge& JavaBridge::instance() {
    static JavaBridge b;
    return b;
}

bool JavaBridge::init(JavaVM* vm) {
    if (vm == nullptr) return false;
    vm_ = vm;
    if (cache_ == nullptr) cache_ = new ClassCache();
    return true;
}

JNIEnv* JavaBridge::env() {
    if (g_tls_env != nullptr) return g_tls_env;
    if (vm_ == nullptr) return nullptr;

    JNIEnv* e = nullptr;
    jint rc = vm_->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6);
    if (rc == JNI_OK) {
        g_tls_env = e;
        g_tls_attached = false;
        return e;
    }
    if (rc == JNI_EDETACHED) {
        JavaVMAttachArgs args = {};
        args.version = JNI_VERSION_1_6;
        args.name    = const_cast<char*>("yamgg-jbridge");
        args.group   = nullptr;
        if (vm_->AttachCurrentThread(&e, &args) == JNI_OK) {
            g_tls_env = e;
            g_tls_attached = true;
            return e;
        }
    }
    return nullptr;
}

jclass JavaBridge::find_class(const char* name) {
    if (name == nullptr || cache_ == nullptr) return nullptr;
    {
        std::lock_guard<std::mutex> lk(cache_->mu);
        auto it = cache_->classes.find(name);
        if (it != cache_->classes.end()) return it->second;
    }
    JNIEnv* e = env();
    if (e == nullptr) return nullptr;
    jclass local = e->FindClass(name);
    if (local == nullptr) {
        if (e->ExceptionCheck()) e->ExceptionClear();
        return nullptr;
    }
    jclass global = static_cast<jclass>(e->NewGlobalRef(local));
    e->DeleteLocalRef(local);
    if (global != nullptr) {
        std::lock_guard<std::mutex> lk(cache_->mu);
        cache_->classes[name] = global;
    }
    return global;
}

// ─── Exception handling ────────────────────────────────────
bool JavaBridge::check_exception(std::string* out_msg) {
    JNIEnv* e = env();
    if (e == nullptr) return false;
    if (!e->ExceptionCheck()) return false;

    jthrowable thr = e->ExceptionOccurred();
    e->ExceptionClear();

    if (out_msg != nullptr) {
        jclass thrClass = e->FindClass("java/lang/Throwable");
        if (thrClass != nullptr) {
            jmethodID toString = e->GetMethodID(thrClass, "toString",
                "()Ljava/lang/String;");
            if (toString != nullptr) {
                auto str = static_cast<jstring>(
                    e->CallObjectMethod(thr, toString));
                if (str != nullptr) {
                    const char* c = e->GetStringUTFChars(str, nullptr);
                    if (c != nullptr) {
                        *out_msg = c;
                        e->ReleaseStringUTFChars(str, c);
                    }
                    e->DeleteLocalRef(str);
                }
            }
            e->DeleteLocalRef(thrClass);
        }
    }
    e->DeleteLocalRef(thr);
    return true;
}

// ─── Field helpers ─────────────────────────────────────────
namespace {
jfieldID get_field_id(JNIEnv* e, jclass cls, const char* name, const char* sig,
                      std::unordered_map<std::string, jfieldID>& cache,
                      std::mutex& mu) {
    std::string key = std::string(name) + ":" + sig;
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    jfieldID fid = e->GetFieldID(cls, name, sig);
    if (fid == nullptr) return nullptr;
    {
        std::lock_guard<std::mutex> lk(mu);
        cache[key] = fid;
    }
    return fid;
}

jmethodID get_method_id(JNIEnv* e, jclass cls, const char* name, const char* sig,
                        std::unordered_map<std::string, jmethodID>& cache,
                        std::mutex& mu) {
    std::string key = std::string(name) + ":" + sig;
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    jmethodID mid = e->GetMethodID(cls, name, sig);
    if (mid == nullptr) return nullptr;
    {
        std::lock_guard<std::mutex> lk(mu);
        cache[key] = mid;
    }
    return mid;
}
} // namespace

bool JavaBridge::get_int_field(jobject obj, const char* cls, const char* field,
                                jint* out, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, "I", cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    *out = e->GetIntField(obj, f);
    return !check_exception(error);
}

bool JavaBridge::get_long_field(jobject obj, const char* cls, const char* field,
                                 jlong* out, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, "J", cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    *out = e->GetLongField(obj, f);
    return !check_exception(error);
}

bool JavaBridge::get_bool_field(jobject obj, const char* cls, const char* field,
                                 jboolean* out, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, "Z", cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    *out = e->GetBooleanField(obj, f);
    return !check_exception(error);
}

bool JavaBridge::get_double_field(jobject obj, const char* cls, const char* field,
                                   jdouble* out, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, "D", cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    *out = e->GetDoubleField(obj, f);
    return !check_exception(error);
}

bool JavaBridge::get_object_field(jobject obj, const char* cls, const char* field,
                                   const char* sig, jobject* out, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, sig, cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    jobject local = e->GetObjectField(obj, f);
    if (local != nullptr) *out = e->NewGlobalRef(local);
    if (local) e->DeleteLocalRef(local);
    return !check_exception(error);
}

bool JavaBridge::get_string_field(jobject obj, const char* cls, const char* field,
                                   std::string* out, std::string* error) {
    jobject o = nullptr;
    if (!get_object_field(obj, cls, field, "Ljava/lang/String;", &o, error)) return false;
    if (o == nullptr) { *out = ""; return true; }
    *out = to_utf8(static_cast<jstring>(o));
    release_global(o);
    return true;
}

bool JavaBridge::set_int_field(jobject obj, const char* cls, const char* field,
                                jint value, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, "I", cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    e->SetIntField(obj, f, value);
    return !check_exception(error);
}

bool JavaBridge::set_long_field(jobject obj, const char* cls, const char* field,
                                 jlong value, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, "J", cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    e->SetLongField(obj, f, value);
    return !check_exception(error);
}

bool JavaBridge::set_bool_field(jobject obj, const char* cls, const char* field,
                                 jboolean value, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, "Z", cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    e->SetBooleanField(obj, f, value);
    return !check_exception(error);
}

bool JavaBridge::set_object_field(jobject obj, const char* cls, const char* field,
                                   const char* sig, jobject value, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jfieldID f = get_field_id(e, c, field, sig, cache_->fields, cache_->mu);
    if (f == nullptr) { check_exception(error); return false; }
    e->SetObjectField(obj, f, value);
    return !check_exception(error);
}

bool JavaBridge::set_string_field(jobject obj, const char* cls, const char* field,
                                   const std::string& value, std::string* error) {
    jstring s = from_utf8(value);
    if (s == nullptr) return false;
    bool ok = set_object_field(obj, cls, field, "Ljava/lang/String;", s, error);
    JNIEnv* e = env(); if (e) e->DeleteLocalRef(s);
    return ok;
}

// ─── Method calls ──────────────────────────────────────────
bool JavaBridge::call_void_method(jobject obj, const char* cls, const char* method,
                                   const char* sig, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jmethodID m = get_method_id(e, c, method, sig, cache_->methods, cache_->mu);
    if (m == nullptr) { check_exception(error); return false; }
    e->CallVoidMethod(obj, m);
    return !check_exception(error);
}

bool JavaBridge::call_int_method(jobject obj, const char* cls, const char* method,
                                  const char* sig, jint* out, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jmethodID m = get_method_id(e, c, method, sig, cache_->methods, cache_->mu);
    if (m == nullptr) { check_exception(error); return false; }
    *out = e->CallIntMethod(obj, m);
    return !check_exception(error);
}

bool JavaBridge::call_object_method(jobject obj, const char* cls, const char* method,
                                     const char* sig, jobject* out, std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return false;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return false; }
    jmethodID m = get_method_id(e, c, method, sig, cache_->methods, cache_->mu);
    if (m == nullptr) { check_exception(error); return false; }
    jobject local = e->CallObjectMethod(obj, m);
    if (local != nullptr) *out = e->NewGlobalRef(local);
    if (local) e->DeleteLocalRef(local);
    return !check_exception(error);
}

bool JavaBridge::call_string_method(jobject obj, const char* cls, const char* method,
                                     const char* sig, std::string* out, std::string* error) {
    jobject o = nullptr;
    if (!call_object_method(obj, cls, method, sig, &o, error)) return false;
    if (o == nullptr) { *out = ""; return true; }
    *out = to_utf8(static_cast<jstring>(o));
    release_global(o);
    return true;
}

// ─── Object creation ──────────────────────────────────────
jobject JavaBridge::new_object(const char* cls, const char* ctor_sig,
                                std::string* error) {
    JNIEnv* e = env(); if (e == nullptr || cache_ == nullptr) return nullptr;
    jclass c = find_class(cls); if (c == nullptr) { if (error) *error = "class not found"; return nullptr; }
    jmethodID m = get_method_id(e, c, "<init>", ctor_sig, cache_->methods, cache_->mu);
    if (m == nullptr) { check_exception(error); return nullptr; }
    jobject local = e->NewObject(c, m);
    jobject global = nullptr;
    if (local != nullptr) global = e->NewGlobalRef(local);
    if (local) e->DeleteLocalRef(local);
    if (check_exception(error)) {
        if (global) { e->DeleteGlobalRef(global); global = nullptr; }
    }
    return global;
}

jstring JavaBridge::new_string(const std::string& s) {
    JNIEnv* e = env(); if (e == nullptr) return nullptr;
    return e->NewStringUTF(s.c_str());
}

// ─── Global refs ─────────────────────────────────────────
jobject JavaBridge::to_global(jobject local) {
    JNIEnv* e = env(); if (e == nullptr || local == nullptr) return nullptr;
    return e->NewGlobalRef(local);
}

void JavaBridge::release_global(jobject global) {
    JNIEnv* e = env(); if (e == nullptr || global == nullptr) return;
    e->DeleteGlobalRef(global);
}

// ─── String conversion ────────────────────────────────────
std::string JavaBridge::to_utf8(jstring str) {
    if (str == nullptr) return "";
    JNIEnv* e = env(); if (e == nullptr) return "";
    const char* c = e->GetStringUTFChars(str, nullptr);
    if (c == nullptr) return "";
    std::string out(c);
    e->ReleaseStringUTFChars(str, c);
    return out;
}

jstring JavaBridge::from_utf8(const std::string& str) {
    JNIEnv* e = env(); if (e == nullptr) return nullptr;
    return e->NewStringUTF(str.c_str());
}

} // namespace native
} // namespace yamgg
