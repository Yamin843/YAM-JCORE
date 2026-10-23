// core/thread_sync.h — cross-thread marshalling primitives.
//
// Two problems this layer solves:
//
//  1. ThreadIgnoreScope. gum_on_thread_realize calls
//     gum_interceptor_ignore_current_thread automatically on every thread
//     created by g_thread_new. But some ART internals expect *no*
//     interceptor hooks at all during specific sequences (e.g. the very
//     first JNI_GetCreatedJavaVMs call). We wrap those with an explicit
//     APPLICATION_THREAD scope, and we wrap worker threads that must not
//     trip the interceptor with FRIDA_THREAD.
//
//  2. UI thread marshalling. The JS thread, the worker thread, and the
//     Android main (UI) thread are separate. ImGui *must* only be
//     touched from the main thread. Any callback originating from JS
//     has to hop back to the main thread before it may call notify() or
//     modify ImGui state.
#ifndef YAMGG_CORE_THREAD_SYNC_H
#define YAMGG_CORE_THREAD_SYNC_H

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

namespace yamgg {

// RAII wrapper around gum_interceptor_ignore_current_thread.
//
//   Application : ignores the interceptor for the *current* thread only.
//                 Use on the main thread around the bootstrap sequence.
//   Frida       : additionally hides the thread from /proc/self/task and
//                 cloaks its memory ranges. Only relevant for threads
//                 you spawn yourself; g_thread_new applies this
//                 automatically.
//
// Both kinds are nestable. Each instance obtains its own reference to
// the interceptor.
class InterceptorIgnoreScope {
public:
    enum class Kind { Application, Frida };

    explicit InterceptorIgnoreScope(Kind kind);
    ~InterceptorIgnoreScope();

    InterceptorIgnoreScope(const InterceptorIgnoreScope&) = delete;
    InterceptorIgnoreScope& operator=(const InterceptorIgnoreScope&) = delete;

private:
    void* interceptor_ = nullptr;
    Kind  kind_;
};

// RAII wrapper that preserves errno across a block, because ART and V8
// both expect errno to survive their own syscalls when they are called
// from JS callbacks that may fire between our syscall and our read.
class ErrnoScope {
public:
    ErrnoScope();
    ~ErrnoScope();

private:
    int saved_errno_;
};

// Marshals a task to the UI thread. Tasks are executed by the UI thread
// when it calls drain() (once per frame from Renderer::onDrawFrame).
//
// Tasks run under the UI thread's normal context, so it is safe to
// touch ImGui, ImGui::GetIO(), and MainWindow from inside them.
class UiThreadQueue {
public:
    static UiThreadQueue& instance();

    using Task = std::function<void()>;

    // Maximum number of pending tasks. When the queue is full, the
    // oldest task is dropped to make room for the new one. This
    // prevents runaway memory growth when JS floods the UI thread
    // with results (e.g. tens of thousands of heap matches).
    static constexpr size_t kMaxQueueSize = 10000;

    // Called by any thread. Enqueues and returns immediately. If the
    // queue is full, the oldest task is discarded.
    void post(Task task);

    // Statistics for the UI to display.
    size_t queue_size() const;
    size_t dropped_count() const;

    // Called by the UI thread once per frame. Executes all queued tasks
    // in FIFO order.
    void drain();

private:
    UiThreadQueue() = default;

    std::mutex                                    mu_;
    std::queue<Task>                              q_;
    mutable std::atomic<size_t>                   dropped_count_{0};
};

// Convenience: schedule a JS reply callback to run on the UI thread.
// The callback receives the raw result as a string so the UI code can
// parse it with nlohmann::json at its leisure.
void marshal_to_ui(std::function<void()> fn);

} // namespace yamgg

#endif // YAMGG_CORE_THREAD_SYNC_H
