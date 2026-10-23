// native/java_bridge.h — direct C++ → Java bridge for performance.
//
// The JS path (MessageRouter::send_command) is flexible and required
// for complex operations (hooks, registerClass, choose, etc.). For
// simple reads and writes done at high frequency — e.g. displaying
// 1000 rows of field data — the round-trip through JS is measurable.
//
// This class provides direct JNI calls that bypass JS entirely. It
// caches:
//   * a thread-local JNIEnv (attached once per thread)
//   * global references for every class it looks up
//   * jmethodID and jfieldID values, keyed by their descriptors
//
// All operations return false and fill `error` on failure. The caller
// is responsible for the `error` string's lifetime (it is a normal
// std::string).
#ifndef YAMGG_NATIVE_JAVA_BRIDGE_H
#define YAMGG_NATIVE_JAVA_BRIDGE_H

#include <jni.h>

#include <cstdint>
#include <string>

namespace yamgg {
namespace native {

class JavaBridge {
public:
    static JavaBridge& instance();

    bool init(JavaVM* vm);

    // Thread-local JNIEnv. Attaches the current thread if needed.
    // Never detaches; the thread should be attached for its lifetime.
    JNIEnv* env();

    // Class lookup. Returns a cached global reference or null.
    jclass find_class(const char* name);

    // ── Field access ────────────────────────────────────────
    bool get_int_field(jobject obj, const char* cls, const char* field,
                       jint* out, std::string* error = nullptr);
    bool get_long_field(jobject obj, const char* cls, const char* field,
                        jlong* out, std::string* error = nullptr);
    bool get_bool_field(jobject obj, const char* cls, const char* field,
                        jboolean* out, std::string* error = nullptr);
    bool get_double_field(jobject obj, const char* cls, const char* field,
                          jdouble* out, std::string* error = nullptr);
    bool get_object_field(jobject obj, const char* cls, const char* field,
                          const char* sig, jobject* out,
                          std::string* error = nullptr);
    bool get_string_field(jobject obj, const char* cls, const char* field,
                          std::string* out, std::string* error = nullptr);

    bool set_int_field(jobject obj, const char* cls, const char* field,
                       jint value, std::string* error = nullptr);
    bool set_long_field(jobject obj, const char* cls, const char* field,
                        jlong value, std::string* error = nullptr);
    bool set_bool_field(jobject obj, const char* cls, const char* field,
                        jboolean value, std::string* error = nullptr);
    bool set_object_field(jobject obj, const char* cls, const char* field,
                          const char* sig, jobject value,
                          std::string* error = nullptr);
    bool set_string_field(jobject obj, const char* cls, const char* field,
                          const std::string& value,
                          std::string* error = nullptr);

    // ── Method calls (simplified, 0-2 args) ────────────────
    bool call_void_method(jobject obj, const char* cls, const char* method,
                          const char* sig, std::string* error = nullptr);
    bool call_int_method(jobject obj, const char* cls, const char* method,
                         const char* sig, jint* out,
                         std::string* error = nullptr);
    bool call_object_method(jobject obj, const char* cls, const char* method,
                            const char* sig, jobject* out,
                            std::string* error = nullptr);
    bool call_string_method(jobject obj, const char* cls, const char* method,
                            const char* sig, std::string* out,
                            std::string* error = nullptr);

    // ── Object creation ────────────────────────────────────
    jobject new_object(const char* cls, const char* ctor_sig,
                       std::string* error = nullptr);
    jstring new_string(const std::string& s);

    // ── Global refs ────────────────────────────────────────
    jobject to_global(jobject local);
    void    release_global(jobject global);

    // ── String conversion ──────────────────────────────────
    std::string to_utf8(jstring str);
    jstring     from_utf8(const std::string& str);

    // ── Exception handling ─────────────────────────────────
    // If an exception is pending, clears it and writes its
    // toString() to *out_msg. Returns true if an exception was
    // pending.
    bool check_exception(std::string* out_msg = nullptr);

private:
    JavaBridge() = default;
    ~JavaBridge() = default;

    bool ensure_ready(std::string* error);

    JavaVM* vm_ = nullptr;

    struct ClassCache;
    ClassCache* cache_ = nullptr;
};

} // namespace native
} // namespace yamgg

#endif // YAMGG_NATIVE_JAVA_BRIDGE_H
