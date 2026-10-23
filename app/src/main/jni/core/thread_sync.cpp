// core/thread_sync.cpp
#include "core/thread_sync.h"
#include "YAMJS.h"

#include <android/log.h>
#include <atomic>
#include <cstdint>
#include <cerrno>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {

// ═══════════════════════════════════════════════════════════════
//  InterceptorIgnoreScope
// ═══════════════════════════════════════════════════════════════

InterceptorIgnoreScope::InterceptorIgnoreScope(Kind kind) : kind_(kind) {
    // Every scope, of every kind, suppresses the interceptor on the
    // current thread. Without this, JNI calls made from inside our own
    // instrumentation (e.g. during Java.perform's class loading) would
    // recursively trigger our own hooks and crash.
    YamInterceptor* ic = yam_interceptor_obtain();
    if (ic != nullptr) {
        yam_interceptor_ignore_current_thread(ic);
        interceptor_ = ic;
    }

    // Frida-thread scopes additionally hide the calling thread from
    // /proc/self/task so that anti-cheat code that enumerates threads
    // does not see our worker. Application-thread scopes must NOT do
    // this: the app's main thread has to stay visible or the runtime
    // itself breaks.
    if (kind_ == Kind::Frida) {
        std::uint32_t tid = static_cast<std::uint32_t>(
            yam_process_get_current_thread_id());
        if (tid != 0) {
            yam_cloak_add_thread(tid);
            frida_thread_id_ = tid;
        }
    }
}

InterceptorIgnoreScope::~InterceptorIgnoreScope() {
    if (kind_ == Kind::Frida && frida_thread_id_ != 0) {
        yam_cloak_remove_thread(frida_thread_id_);
        frida_thread_id_ = 0;
    }
    if (interceptor_ != nullptr) {
        YamInterceptor* ic = static_cast<YamInterceptor*>(interceptor_);
        yam_interceptor_unignore_current_thread(ic);
        g_object_unref(ic);
        interceptor_ = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════
//  ErrnoScope
// ═══════════════════════════════════════════════════════════════

ErrnoScope::ErrnoScope() : saved_errno_(errno) {}
ErrnoScope::~ErrnoScope() { errno = saved_errno_; }

// ═══════════════════════════════════════════════════════════════
//  UiThreadQueue
// ═══════════════════════════════════════════════════════════════

UiThreadQueue& UiThreadQueue::instance() {
    static UiThreadQueue q;
    return q;
}

void UiThreadQueue::post(Task task) {
    if (!task) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (q_.size() >= kMaxQueueSize) {
        // Drop the oldest task to make room. This is the correct
        // policy for progress-style events: the newest is always the
        // most relevant.
        q_.pop();
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
    }
    q_.push(std::move(task));
}

size_t UiThreadQueue::queue_size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return q_.size();
}

size_t UiThreadQueue::dropped_count() const {
    return dropped_count_.load(std::memory_order_relaxed);
}

void UiThreadQueue::drain() {
    // Swap the queue under the lock, then run tasks outside the lock so
    // that a task posting another task during execution is safe.
    std::queue<Task> local;
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::swap(local, q_);
    }
    while (!local.empty()) {
        Task t = std::move(local.front());
        local.pop();
        try { t(); }
        catch (const std::exception& e) {
            LOGW("UiThreadQueue::drain: task threw: %s", e.what());
        } catch (...) {
            LOGW("UiThreadQueue::drain: task threw unknown");
        }
    }
}

void marshal_to_ui(std::function<void()> fn) {
    UiThreadQueue::instance().post(std::move(fn));
}

} // namespace yamgg
