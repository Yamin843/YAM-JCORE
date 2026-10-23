// ui/notification.cpp
#include "ui/notification.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <vector>

#include "imgui.h"

namespace yamgg {

namespace {
constexpr float  kFadeSeconds = 0.6f;
constexpr float  kToastMargin = 12.0f;
constexpr float  kToastSpacing = 6.0f;
}

Notification& Notification::instance() {
    static Notification n;
    return n;
}

void Notification::push(const std::string& msg, float duration_seconds) {
    if (msg.empty()) return;
    std::lock_guard<std::mutex> lk(mu_);

    Item it;
    it.id       = next_id_++;
    it.msg      = msg;
    it.created  = std::chrono::steady_clock::now();
    it.duration = (duration_seconds > 0.1f) ? duration_seconds : 3.0f;

    items_.push_back(std::move(it));

}

void Notification::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    items_.clear();
}

size_t Notification::count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return items_.size();
}

void Notification::draw() {
    // Take a snapshot under the lock; draw outside the lock so that a
    // push() from another thread does not stall the render loop.
    std::vector<Item> snapshot;
    {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(mu_);

        // Drop expired items from the head.
        while (!items_.empty()) {
            float age = std::chrono::duration<float>(now - items_.front().created).count();
            if (age > items_.front().duration) {
                items_.pop_front();
            } else {
                break;
            }
        }

        snapshot.assign(items_.begin(), items_.end());
    }

    if (snapshot.empty()) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp == nullptr) return;

    const float screen_right = vp->WorkPos.x + vp->WorkSize.x - kToastMargin;
    const float top         = vp->WorkPos.y + kToastMargin;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.65f, 0.00f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,   ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    // Draw newest at the top.
    auto now = std::chrono::steady_clock::now();
    float y = top;

    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
        float age   = std::chrono::duration<float>(now - it->created).count();
        float alpha = 1.0f;
        float fade_start = it->duration - kFadeSeconds;
        if (fade_start > 0.0f && age > fade_start) {
            alpha = 1.0f - (age - fade_start) / kFadeSeconds;
            if (alpha < 0.0f) alpha = 0.0f;
        }

        ImGui::SetNextWindowBgAlpha(0.94f * alpha);
        ImGui::SetNextWindowPos(ImVec2(screen_right, y), ImGuiCond_Always,
                                ImVec2(1.0f, 0.0f));

        char window_id[64];
        std::snprintf(window_id, sizeof(window_id), "##toast_%llu",
                      (unsigned long long)it->id);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoInputs
            | ImGuiWindowFlags_NoNav;

        // Limit the message width to 60% of the viewport.
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(120.0f, 0.0f),
            ImVec2(vp->WorkSize.x * 0.60f, FLT_MAX));

        if (ImGui::Begin(window_id, nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(1.0f, 1.0f, 1.0f, alpha));
            ImGui::TextWrapped("%s", it->msg.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();

        // Advance y by the toast's actual height plus spacing.
        y += ImGui::GetItemRectSize().y + kToastSpacing;
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace yamgg
