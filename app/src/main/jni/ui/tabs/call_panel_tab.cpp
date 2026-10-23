// ui/tabs/call_panel_tab.cpp
#include "ui/tabs/call_panel_tab.h"

#include <cstdio>
#include <thread>
#include <cstring>

#include "bridge/call_engine.h"
#include "bridge/handle_registry.h"
#include "bridge/type_converter.h"
#include "core/message_router.h"
#include "core/thread_sync.h"
#include "imgui.h"

namespace yamgg {

CallPanelTab& CallPanelTab::instance() {
    static CallPanelTab t;
    return t;
}

void CallPanelTab::set_instance_created_callback(InstanceCreatedCallback cb) {
    instance_created_cb_ = std::move(cb);
}

char* CallPanelTab::this_handle_buf_for_ui() {
    return this_handle_buf_;
}

void CallPanelTab::prepare_call(const std::string& class_name,
                                const std::string& method_name,
                                const std::vector<std::string>& argument_types,
                                bool is_static,
                                const std::string& this_handle) {
    class_name_     = class_name;
    method_name_    = method_name;
    argument_types_ = argument_types;
    is_static_      = is_static;
    is_ctor_        = (method_name == "$new");

    std::snprintf(class_buf_,  sizeof(class_buf_),  "%s", class_name.c_str());
    std::snprintf(method_buf_, sizeof(method_buf_), "%s", method_name.c_str());
    std::snprintf(this_handle_buf_, sizeof(this_handle_buf_), "%s",
                  this_handle.c_str());

    arg_text_.assign(argument_types_.size(), std::string());
    has_result_ = false;
    result_value_ = nlohmann::json();
    result_error_.clear();
    last_handle_.clear();
}

void CallPanelTab::check_timeout() {
    if (in_flight_ &&
        std::chrono::steady_clock::now() - started_ >
        std::chrono::seconds(timeout_seconds_)) {
        in_flight_    = false;
        result_ok_    = false;
        result_error_ = "timeout";
        has_result_   = true;
    }
}

void CallPanelTab::invoke() {
    if (in_flight_) return;

    nlohmann::json args_json = nlohmann::json::array();
    std::string error;
    for (size_t i = 0; i < argument_types_.size(); i++) {
        nlohmann::json encoded = bridge::TypeConverter::encode_user_input(
            argument_types_[i], arg_text_[i], error);
        if (!error.empty()) {
            result_ok_    = false;
            result_error_ = "arg " + std::to_string(i) + ": " + error;
            has_result_   = true;
            return;
        }
        args_json.push_back(encoded);
    }

    in_flight_ = true;
    started_   = std::chrono::steady_clock::now();
    has_result_ = false;

    std::string cls = class_buf_;
    std::string mth = method_buf_;
    std::string handle = this_handle_buf_;
    bool is_static = is_static_ || is_ctor_;

    // Run on a worker thread so the UI never blocks.
    std::thread([this, cls, mth, handle, is_static, args_json]() {
        nlohmann::json result;
        std::string err;
        bool ok = false;

        if (mth == "$new") {
            std::vector<std::string> ctor_types = argument_types_;
            std::string new_handle;
            ok = bridge::CallEngine::instance().create_instance(
                cls, ctor_types, args_json, new_handle, err, 10000);
            if (ok) {
                result = nlohmann::json{{"handle", new_handle}};
            }
        } else if (is_static) {
            ok = bridge::CallEngine::instance().call_static(
                cls, mth, argument_types_, args_json, result, err, 10000);
        } else {
            if (handle.empty()) {
                err = "thisHandle required for instance method";
                ok = false;
            } else {
                ok = bridge::CallEngine::instance().call_instance(
                    cls, mth, argument_types_, handle, args_json,
                    result, err, 10000);
            }
        }

        marshal_to_ui([this, ok, result, err]() {
            in_flight_    = false;
            result_ok_    = ok;
            has_result_   = true;
            if (ok) {
                result_value_ = result;
                result_error_.clear();
                if (result.is_object() && result.contains("handle") &&
                    result["handle"].is_string()) {
                    last_handle_ = result["handle"].get<std::string>();
                    if (instance_created_cb_) {
                        auto cb = instance_created_cb_;
                        std::string cls_copy = class_name_;
                        std::string h_copy = last_handle_;
                        marshal_to_ui([cb, cls_copy, h_copy]() {
                            cb(cls_copy, h_copy);
                        });
                    }
                }
            } else {
                result_value_ = nlohmann::json();
                result_error_ = err.empty() ? "call failed" : err;
            }
        });
    }).detach();
}

void CallPanelTab::release_handle(const std::string& handle) {
    if (handle.empty()) return;
    std::string error;
    bridge::HandleRegistry::instance().release(handle, error, 4000);
    for (auto it = handles_cache_.begin(); it != handles_cache_.end(); ++it) {
        if (it->handle == handle) {
            handles_cache_.erase(it);
            break;
        }
    }
    if (last_handle_ == handle) last_handle_.clear();
}

void CallPanelTab::release_all_handles() {
    bridge::HandleRegistry::instance().release_all();
    handles_cache_.clear();
    last_handle_.clear();
}

// ═══════════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════════

void CallPanelTab::draw() {
    check_timeout();

    draw_target_section();
    ImGui::Separator();
    draw_args_section();
    ImGui::Separator();
    draw_result_section();
    ImGui::Separator();
    draw_handles_section();
}

void CallPanelTab::draw_target_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Target");

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 8.0f);
    ImGui::InputTextWithHint("##cls", "Class", class_buf_, sizeof(class_buf_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##mth", "Method (or $new)", method_buf_, sizeof(method_buf_));

    ImGui::Checkbox("Static", &is_static_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##thishandle", "thisHandle (required for instance methods)",
                             this_handle_buf_, sizeof(this_handle_buf_));

    if (ImGui::Button("Use last handle") && !last_handle_.empty()) {
        std::snprintf(this_handle_buf_, sizeof(this_handle_buf_), "%s",
                      last_handle_.c_str());
    }
}

void CallPanelTab::draw_args_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Arguments");

    if (argument_types_.empty()) {
        ImGui::TextDisabled("(no arguments)");
        return;
    }

    for (size_t i = 0; i < argument_types_.size(); i++) {
        ImGui::PushID((int)i);
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "[%zu] %s",
                           i, argument_types_[i].c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        char buf[1024];
        std::snprintf(buf, sizeof(buf), "%s", arg_text_[i].c_str());
        if (ImGui::InputText("##arg", buf, sizeof(buf))) {
            arg_text_[i] = buf;
        }
        ImGui::PopID();
    }

    ImGui::Separator();

    if (in_flight_) {
        ImGui::TextDisabled("Invoking…");
    } else {
        if (ImGui::Button("Invoke", ImVec2(140.0f, 0.0f))) {
            invoke();
        }
    }
}

void CallPanelTab::draw_result_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Result");

    if (!has_result_) {
        ImGui::TextDisabled("(no result yet)");
        return;
    }

    if (!result_ok_) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s",
                           result_error_.c_str());
        return;
    }

    std::string display;
    try { display = bridge::TypeConverter::display(result_value_); }
    catch (...) { display = "<unprintable>"; }

    ImGui::TextWrapped("%s", display.c_str());

    if (result_value_.is_string()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) {
            // Clipboard handling is done in ModView.java's hidden EditText;
            // we use the notification system as a proxy.
            // (Real clipboard copy is wired in Reply 7 via JNI.)
        }
    }
}

void CallPanelTab::draw_handles_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Retained handles (%zu)",
                       bridge::HandleRegistry::instance().count());

    if (ImGui::Button("Release all")) {
        release_all_handles();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##handlefilter", "Filter handles…",
                             handle_filter_buf_, sizeof(handle_filter_buf_));

    // Refresh the cache lazily: the list of handles is small and mostly
    // stable, so we refresh every ~30 frames.
    static int refresh_countdown = 0;
    if (++refresh_countdown >= 30) {
        refresh_countdown = 0;
        handles_cache_.clear();
        // HandleRegistry does not expose iteration; we rely on the
        // last_handle_ plus a synthetic "refresh" notification instead.
        if (!last_handle_.empty()) {
            handles_cache_.push_back({last_handle_, ""});
        }
    }

    if (handles_cache_.empty()) {
        ImGui::TextDisabled("(none tracked in UI)");
        return;
    }

    std::string filter = handle_filter_buf_;
    for (size_t i = 0; i < handles_cache_.size(); i++) {
        const auto& e = handles_cache_[i];
        if (!filter.empty() && e.handle.find(filter) == std::string::npos) continue;

        ImGui::PushID((int)i);
        ImGui::TextUnformatted(e.handle.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Release")) {
            release_handle(e.handle);
        }
        ImGui::PopID();
    }
}


// Free function so other translation units (e.g. RegisterClassTab) can
// copy a handle string into the CallPanelTab's this_handle buffer without
// needing the CallPanelTab header.
extern "C" char* yamgg_call_panel_this_handle_buf() {
    return CallPanelTab::instance().this_handle_buf_for_ui();
}

} // namespace yamgg
