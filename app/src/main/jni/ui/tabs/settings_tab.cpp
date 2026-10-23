// ui/tabs/settings_tab.cpp
#include "ui/tabs/settings_tab.h"

#include "art/art_guard.h"
#include "bridge/handle_registry.h"
#include "core/bootstrap.h"
#include "core/message_router.h"
#include "core/thread_sync.h"
#include "core/error_recovery.h"
#include "imgui.h"
#include "ui/main_window.h"
#include "ui/theme.h"

namespace yamgg {

SettingsTab& SettingsTab::instance() {
    static SettingsTab t;
    return t;
}

void SettingsTab::draw() {
    draw_session_section();
    ImGui::Separator();
    draw_capabilities_section();
    ImGui::Separator();
    draw_deoptimize_section();
    ImGui::Separator();
    draw_handles_section();
    ImGui::Separator();
    draw_ui_section();
}

void SettingsTab::draw_deoptimize_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "ART Deoptimization");
    ImGui::TextDisabled("Forces ART to abandon optimized code. "
                        "Makes hooking reliable on modern Android.");

    static std::string last_result;
    static bool in_flight = false;

    if (in_flight) {
        ImGui::TextDisabled("working…");
        return;
    }

    if (ImGui::Button("Deoptimize Everything")) {
        in_flight = true;
        last_result = "requested";
        MessageRouter::instance().post_command(
            "bridge.deoptimize.everything",
            nlohmann::json::object(),
            [](bool ok, const nlohmann::json& reply) {
                marshal_to_ui([ok, reply]() {
                    last_result = ok ? "ok" : reply.value("error", std::string("error"));
                    in_flight = false;
                });
            });
    }

    ImGui::SameLine();

    if (ImGui::Button("Deoptimize Boot Image")) {
        in_flight = true;
        last_result = "requested";
        MessageRouter::instance().post_command(
            "bridge.deoptimize.boot_image",
            nlohmann::json::object(),
            [](bool ok, const nlohmann::json& reply) {
                marshal_to_ui([ok, reply]() {
                    last_result = ok ? "ok" : reply.value("error", std::string("error"));
                    in_flight = false;
                });
            });
    }

    if (!last_result.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("→ %s", last_result.c_str());
    }
}

namespace {

const char* session_state_name(SessionState s) {
    switch (s) {
        case SessionState::Created:      return "created";
        case SessionState::Initialising: return "initialising";
        case SessionState::Ready:        return "ready";
        case SessionState::Running:      return "running";
        case SessionState::Shutting:     return "shutting";
        case SessionState::Dead:         return "dead";
    }
    return "unknown";
}

ImVec4 capability_color(Capability c) {
    switch (c) {
        case Capability::Full:      return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        case Capability::ArtOnly:   return ImVec4(0.9f, 0.9f, 0.4f, 1.0f);
        case Capability::JvmtiOnly: return ImVec4(0.9f, 0.7f, 0.3f, 1.0f);
        case Capability::JniOnly:   return ImVec4(0.9f, 0.5f, 0.3f, 1.0f);
        case Capability::Dead:      return ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

} // namespace

void SettingsTab::draw_session_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Session");

    SessionState st = current_state();
    ImGui::Text("State: %s (%d)", session_state_name(st), (int)st);

    const BridgeInfo& bi = bridge_info();
    ImGui::Text("Bridge: reported=%s available=%s variant='%s' api=%d",
                bi.reported ? "yes" : "no",
                bi.available ? "yes" : "no",
                bi.variant.c_str(), bi.apiLevel);

    Capability cap = current_capability();
    ImGui::TextColored(capability_color(cap), "Capability: %s",
                       capability_name(cap));
}

void SettingsTab::draw_capabilities_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "ART Capabilities");

    const art::ArtGuard& g = art::ArtGuard::instance();
    if (!g.is_initialized()) {
        ImGui::TextDisabled("not yet initialised");
        return;
    }

    const auto& bridge = g.bridge();
    const auto& jvmti  = g.jvmti();
    const auto& art    = g.art();

    ImGui::Text("bridge.available: %s", bridge.available ? "yes" : "no");
    ImGui::Text("bridge.hasJava:   %s", bridge.hasJava   ? "yes" : "no");
    ImGui::Text("bridge.variant:   %s", bridge.variant.c_str());
    ImGui::Text("bridge.apiLevel:  %d", bridge.apiLevel);

    ImGui::Separator();
    ImGui::Text("jvmti.available:  %s", jvmti.available ? "yes" : "no");
    ImGui::Text("jvmti.canTag:     %s", jvmti.canTagObjects ? "yes" : "no");

    ImGui::Separator();
    ImGui::Text("art.flavor:       %s", art.flavor.c_str());
    ImGui::Text("art.hasInternals: %s", art.hasArtInternals ? "yes" : "no");
}

void SettingsTab::draw_handles_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Handles");

    size_t n = bridge::HandleRegistry::instance().count();
    ImGui::Text("Retained: %zu", n);
    ImGui::Text("Capacity: %zu", bridge::HandleRegistry::instance().capacity());

    if (ImGui::Button("Release all")) {
        bridge::HandleRegistry::instance().release_all();
    }
}

void SettingsTab::draw_ui_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "UI");

    float fs = Theme::font_size();
    ImGui::Text("Font size: %.1f", fs);
    ImGui::SameLine();
    if (ImGui::Button("Smaller") && fs > 10.0f) {
        Theme::set_font_size(fs - 1.0f);
        Theme::reload_font();
    }
    ImGui::SameLine();
    if (ImGui::Button("Larger") && fs < 32.0f) {
        Theme::set_font_size(fs + 1.0f);
        Theme::reload_font();
    }

    bool full = MainWindow::instance().is_fullscreen();
    if (ImGui::Checkbox("Fullscreen", &full)) {
        MainWindow::instance().set_fullscreen(full);
    }
}

} // namespace yamgg
