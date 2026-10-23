// ui/tabs/settings_tab.cpp
#include "ui/tabs/settings_tab.h"

#include "art/art_guard.h"
#include "bridge/handle_registry.h"
#include "core/bootstrap.h"
#include "core/message_router.h"
#include "core/thread_sync.h"
#include "core/error_recovery.h"
#include "core/message_router.h"
#include "core/thread_sync.h"

#include <json.hpp>
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
    draw_vm_info_section();
    ImGui::Separator();
    draw_classloaders_section();
    ImGui::Separator();
    draw_open_class_file_section();
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


// ═══════════════════════════════════════════════════════════════
//  VM info
// ═══════════════════════════════════════════════════════════════

void SettingsTab::draw_vm_info_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Java VM Info");

    if (ImGui::Button("Query VM info")) {
        vm_info_requested_ = true;
        vm_info_text_.clear();
        vm_info_error_.clear();
        MessageRouter::instance().post_command(
            "bridge.java.vm_info",
            nlohmann::json::object(),
            [this](bool ok, const nlohmann::json& reply) {
                marshal_to_ui([this, ok, reply]() {
                    vm_info_requested_ = false;
                    if (!ok) {
                        vm_info_error_ = reply.value("error", std::string("unknown"));
                        return;
                    }
                    try { vm_info_text_ = reply.dump(2); }
                    catch (...) { vm_info_text_ = "<unparseable>"; }
                });
            });
    }

    if (vm_info_requested_) {
        ImGui::SameLine();
        ImGui::TextDisabled("querying…");
    }

    if (!vm_info_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "error: %s",
                           vm_info_error_.c_str());
    }
    if (!vm_info_text_.empty()) {
        ImGui::BeginChild("##vm_info", ImVec2(0, 160), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(vm_info_text_.c_str());
        ImGui::EndChild();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Class loaders
// ═══════════════════════════════════════════════════════════════

void SettingsTab::draw_classloaders_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                       "Class Loaders (%zu)", loaders_cache_.size());

    if (ImGui::Button("Refresh loaders")) {
        loaders_loading_ = true;
        loaders_error_.clear();
        MessageRouter::instance().post_command(
            "bridge.classloaders.list",
            nlohmann::json::object(),
            [this](bool ok, const nlohmann::json& reply) {
                marshal_to_ui([this, ok, reply]() {
                    loaders_loading_ = false;
                    if (!ok) {
                        loaders_error_ = reply.value("error", std::string("unknown"));
                        return;
                    }
                    loaders_cache_.clear();
                    if (reply.contains("loaders") && reply["loaders"].is_array()) {
                        for (const auto& e : reply["loaders"]) {
                            LoaderEntry rec;
                            rec.handle      = e.value("handle", std::string());
                            rec.description = e.value("description", std::string());
                            loaders_cache_.push_back(std::move(rec));
                        }
                    }
                });
            });
    }

    if (loaders_loading_) {
        ImGui::SameLine();
        ImGui::TextDisabled("loading…");
    }

    if (!loaders_error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "error: %s",
                           loaders_error_.c_str());
    }

    if (!loaders_cache_.empty()) {
        ImGui::BeginChild("##loaders", ImVec2(0, 140), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < loaders_cache_.size(); i++) {
            const auto& e = loaders_cache_[i];
            ImGui::PushID((int)i);
            ImGui::Text("%zu. %s", i, e.description.c_str());
            if (!e.handle.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Use in factory")) {
                    // Create a ClassFactory bound to this loader.
                    nlohmann::json args = {{"loaderHandle", e.handle}};
                    MessageRouter::instance().post_command(
                        "bridge.java.new_class_factory", args,
                        [](bool ok2, const nlohmann::json& reply2) {
                            if (!ok2) return;
                            // Report success via log.
                        });
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Open class file (Java.openClassFile)
// ═══════════════════════════════════════════════════════════════

void SettingsTab::draw_open_class_file_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                       "Open Class File (DEX)");

    ImGui::TextDisabled("Path to a DEX file on the device:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##dex_path", open_dex_path_buf_,
                     sizeof(open_dex_path_buf_));

    if (open_dex_in_flight_) {
        ImGui::TextDisabled("loading…");
    } else {
        if (ImGui::Button("Load DEX")) {
            if (open_dex_path_buf_[0] != '\0') {
                open_dex_in_flight_ = true;
                open_dex_result_.clear();
                nlohmann::json args = {{"path", std::string(open_dex_path_buf_)}};
                MessageRouter::instance().post_command(
                    "bridge.java.open_class_file", args,
                    [this](bool ok, const nlohmann::json& reply) {
                        marshal_to_ui([this, ok, reply]() {
                            open_dex_in_flight_ = false;
                            if (!ok) {
                                open_dex_result_ = "error: " +
                                    reply.value("error", std::string("unknown"));
                                return;
                            }
                            open_dex_result_ =
                                "loaded; handle = " +
                                reply.value("handle", std::string());
                        });
                    });
            }
        }
    }

    if (!open_dex_result_.empty()) {
        ImGui::TextWrapped("%s", open_dex_result_.c_str());
    }
}

} // namespace yamgg
