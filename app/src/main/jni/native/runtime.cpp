// native/runtime.cpp
#include "native/runtime.h"

#include <mutex>

namespace yamgg {
namespace native {

namespace {
std::mutex g_mu;
}

Runtime& Runtime::instance() {
    static Runtime r;
    return r;
}

bool Runtime::initialize(JavaVM* vm) {
    if (vm == nullptr) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    vm_ = vm;
    initialized_ = true;
    return true;
}

void Runtime::shutdown() {
    std::lock_guard<std::mutex> lk(g_mu);
    vm_ = nullptr;
    initialized_ = false;
}

bool Runtime::is_initialized() const {
    std::lock_guard<std::mutex> lk(g_mu);
    return initialized_ && vm_ != nullptr;
}

JNIEnv* Runtime::get_env() {
    JavaVM* vm = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        vm = vm_;
    }
    if (vm == nullptr) return nullptr;

    JNIEnv* env = nullptr;
    jint rc = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (rc == JNI_OK) return env;
    if (rc == JNI_EDETACHED) {
        JavaVMAttachArgs args = {};
        args.version = JNI_VERSION_1_6;
        args.name    = const_cast<char*>("yamgg-worker");
        args.group   = nullptr;
        if (vm->AttachCurrentThread(&env, &args) == JNI_OK) {
            return env;
        }
    }
    return nullptr;
}

bool Runtime::attach(JNIEnv** env, bool* attached_now) {
    if (attached_now) *attached_now = false;
    if (env) *env = nullptr;

    JavaVM* vm = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        vm = vm_;
    }
    if (vm == nullptr) return false;

    jint rc = vm->GetEnv(reinterpret_cast<void**>(env), JNI_VERSION_1_6);
    if (rc == JNI_OK) return true;
    if (rc == JNI_EDETACHED) {
        JavaVMAttachArgs args = {};
        args.version = JNI_VERSION_1_6;
        args.name    = const_cast<char*>("yamgg-worker");
        args.group   = nullptr;
        if (vm->AttachCurrentThread(env, &args) == JNI_OK) {
            if (attached_now) *attached_now = true;
            return true;
        }
    }
    return false;
}

void Runtime::detach() {
    JavaVM* vm = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        vm = vm_;
    }
    if (vm) vm->DetachCurrentThread();
}

JavaVM* Runtime::vm() const {
    std::lock_guard<std::mutex> lk(g_mu);
    return vm_;
}

// ── ThreadAttachScope ────────────────────────────────────────

ThreadAttachScope::ThreadAttachScope() {
    Runtime::instance().attach(&env_, &attached_now_);
}

ThreadAttachScope::~ThreadAttachScope() {
    if (attached_now_) {
        Runtime::instance().detach();
    }
}

JNIEnv* ThreadAttachScope::env() const {
    return env_;
}

} // namespace native
} // namespace yamgg
