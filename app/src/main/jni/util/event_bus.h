// util/event_bus.h — typed publish/subscribe with no limits.
//
// Subscribers register per topic name. dispatch() delivers to all
// registered subscribers in registration order. Unsubscribe is safe to
// call from within a dispatch (the list is snapshotted before iterating).
//
// There is no cap on subscribers per topic and no cap on topics.
#ifndef YAMGG_UTIL_EVENT_BUS_H
#define YAMGG_UTIL_EVENT_BUS_H

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace yamgg {
namespace util {

class EventBus {
public:
    using Handler = std::function<void(const std::string&)>;

    static EventBus& instance();

    // Register `cb` for `topic`. Returns an ID that can be used to
    // unsubscribe.
    uint64_t subscribe(const std::string& topic, Handler cb);

    // Unregister a specific subscription.
    void unsubscribe(uint64_t id);

    // Unregister all subscriptions for a topic.
    void unsubscribe_all(const std::string& topic);

    // Deliver `payload` to all subscribers of `topic`.
    void dispatch(const std::string& topic, const std::string& payload);

private:
    EventBus() = default;

    struct Entry {
        uint64_t id;
        Handler  cb;
    };

    std::mutex                                mu_;
    std::map<std::string, std::vector<Entry>> subs_;
    uint64_t                                  next_id_ = 1;
};

} // namespace util
} // namespace yamgg

#endif // YAMGG_UTIL_EVENT_BUS_H
