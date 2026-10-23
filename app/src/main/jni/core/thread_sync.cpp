// core/thread_sync.cpp
#include "core/thread_sync.h"
#include "YAMJS.h"

#include <android/log.h>
#include <atomic>
#include <cerrno>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {

// ═══════════════════════════════════════════════════════════════
//  InterceptorIgnoreScope
// ═══════════════════════════════════════════════════════════════

InterceptorIgnoreScope::InterceptorIgnoreScope(Kind kind) : kind_(kind) {
    YamInterceptor* ic = yam_interceptor_obtain();
    if (ic != nullptr) {
        yam_interceptor_ignore_current_thread(ic);
        interceptor_ = ic;
    }
}

InterceptorIgnoreScope::~InterceptorIgnoreScope() {
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
    q_.push(std::move(task));
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
