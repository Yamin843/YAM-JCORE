// ui/tabs/register_class_tab.cpp
#include "ui/tabs/register_class_tab.h"

#include <cstdio>
#include <cstring>

#include <json.hpp>

#include "core/message_router.h"
#include "core/thread_sync.h"
#include "imgui.h"

namespace yamgg {

RegisterClassTab& RegisterClassTab::instance() {
    static RegisterClassTab t;
    return t;
}

void RegisterClassTab::draw() {
    if (in_flight_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started_).count();
        if (elapsed > 30) {
            in_flight_      = false;
            result_ok_      = false;
            result_message_ = "timeout after 30s";
        }
    }

    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                       "Register a Java class at runtime");

    ImGui::TextDisabled(
        "Uses Java.registerClass. The body is JSON with superClass,\n"
        "implements, fields, and methods. Method implementations are\n"
        "JavaScript source wrapped in a function() { ... }.");

    ImGui::Separator();

    // ── Class name ──
    ImGui::TextDisabled("Class name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##class_name", class_name_buf_,
                     sizeof(class_name_buf_));

    // ── Body JSON ──
    ImGui::TextDisabled("Body JSON:");
    ImGui::InputTextMultiline("##body", body_buf_, sizeof(body_buf_),
                              ImVec2(-1, ImGui::GetTextLineHeight() * 20));

    // ── Result display ──
    if (!result_message_.empty()) {
        if (result_ok_) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               "✓ %s", result_message_.c_str());
            if (!result_handle_.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("handle: %s", result_handle_.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy handle")) {
                    copy_handle_to_clipboard();
                }
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "✗ %s", result_message_.c_str());
        }
    }

    // ── Actions ──
    if (in_flight_) {
        ImGui::TextDisabled("registering…");
    } else {
        if (ImGui::Button("Register Class", ImVec2(180.0f, 0.0f))) {
            register_class();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear result")) {
            result_message_.clear();
            result_handle_.clear();
            result_ok_ = false;
        }
    }
}

void RegisterClassTab::register_class() {
    if (in_flight_) return;

    // Parse the body JSON. If invalid, show error inline.
    nlohmann::json body = nlohmann::json::parse(body_buf_, nullptr, false);
    if (body.is_discarded()) {
        result_ok_      = false;
        result_message_ = "Invalid JSON in body";
        result_handle_.clear();
        return;
    }
    if (!body.is_object()) {
        result_ok_      = false;
        result_message_ = "Body must be a JSON object";
        result_handle_.clear();
        return;
    }

    // Merge the class name into the body.
    body["name"] = std::string(class_name_buf_);

    nlohmann::json args = {{"spec", body}};

    in_flight_      = true;
    started_        = std::chrono::steady_clock::now();
    result_ok_      = false;
    result_message_ = "registering…";
    result_handle_.clear();

    MessageRouter::instance().post_command(
        "bridge.register.class",
        args,
        [this](bool ok, const nlohmann::json& reply) {
            marshal_to_ui([this, ok, reply]() {
                in_flight_ = false;
                if (!ok) {
                    result_ok_      = false;
                    result_message_ = reply.value("error", std::string("unknown error"));
                    return;
                }
                result_ok_      = true;
                result_message_ = "registered " + reply.value("className", std::string("?"));
                result_handle_  = reply.value("handle", std::string());
            });
        });
}

extern "C" char* yamgg_call_panel_this_handle_buf();

void RegisterClassTab::copy_handle_to_clipboard() {
    if (result_handle_.empty()) return;
    char* buf = yamgg_call_panel_this_handle_buf();
    if (buf != nullptr) {
        std::snprintf(buf, 64, "%s", result_handle_.c_str());
    }
}

} // namespace yamgg
