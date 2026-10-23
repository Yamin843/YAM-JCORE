// util/event_bus.cpp
#include "util/event_bus.h"

namespace yamgg {
namespace util {

EventBus& EventBus::instance() {
    static EventBus b;
    return b;
}

uint64_t EventBus::subscribe(const std::string& topic, Handler cb) {
    if (topic.empty() || !cb) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    uint64_t id = next_id_++;
    subs_[topic].push_back(Entry{id, std::move(cb)});
    return id;
}

void EventBus::unsubscribe(uint64_t id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& kv : subs_) {
        auto& v = kv.second;
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it->id == id) { v.erase(it); return; }
        }
    }
}

void EventBus::unsubscribe_all(const std::string& topic) {
    std::lock_guard<std::mutex> lk(mu_);
    subs_.erase(topic);
}

void EventBus::dispatch(const std::string& topic, const std::string& payload) {
    std::vector<Handler> handlers;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = subs_.find(topic);
        if (it == subs_.end()) return;
        handlers.reserve(it->second.size());
        for (const auto& e : it->second) handlers.push_back(e.cb);
    }
    for (auto& h : handlers) {
        try { h(payload); } catch (...) {}
    }
}

} // namespace util
} // namespace yamgg
