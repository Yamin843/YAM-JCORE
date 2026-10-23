// ui/tab.cpp — TabRegistry implementation.
#include "ui/tab.h"

namespace yamgg {

TabRegistry& TabRegistry::instance() {
    static TabRegistry r;
    return r;
}

void TabRegistry::register_tab(std::shared_ptr<Tab> tab) {
    if (!tab) return;
    std::lock_guard<std::mutex> lk(mu_);
    tabs_.push_back(std::move(tab));
}

size_t TabRegistry::count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return tabs_.size();
}

Tab* TabRegistry::get(size_t index) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (index >= tabs_.size()) return nullptr;
    return tabs_[index].get();
}

std::vector<Tab*> TabRegistry::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Tab*> out;
    out.reserve(tabs_.size());
    for (const auto& t : tabs_) out.push_back(t.get());
    return out;
}

void TabRegistry::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    tabs_.clear();
}

} // namespace yamgg
