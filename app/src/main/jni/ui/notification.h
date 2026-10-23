// ui/notification.h — stacked toast notifications with fade-out.
//
// Thread-safe push(): callable from any thread including the JS thread.
// Drawing happens on the UI thread only.
//
// Cap is 8 concurrent toasts; older ones are dropped when the cap is
// exceeded. Each toast lasts `duration` seconds; the last 0.6s are a
// linear fade-out.
#ifndef YAMGG_UI_NOTIFICATION_H
#define YAMGG_UI_NOTIFICATION_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace yamgg {

class Notification {
public:
    static Notification& instance();

    // Push a message. Thread-safe.
    void push(const std::string& msg, float duration_seconds = 3.0f);

    // Clear all toasts. Thread-safe.
    void clear();

    // Draw the toasts. UI thread only (called by MainWindow::draw).
    void draw();

    // Number of live toasts. Thread-safe.
    size_t count() const;

private:
    Notification() = default;

    struct Item {
        uint64_t                                   id;
        std::string                                msg;
        std::chrono::steady_clock::time_point      created;
        float                                      duration;
    };

    mutable std::mutex     mu_;
    std::deque<Item>       items_;
    uint64_t               next_id_ = 1;
};

} // namespace yamgg

#endif // YAMGG_UI_NOTIFICATION_H
