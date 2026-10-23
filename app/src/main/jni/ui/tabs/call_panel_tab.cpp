// ui/tabs/call_panel_tab.cpp
#include "ui/tabs/call_panel_tab.h"

#include <cstdio>
#include <cstring>
#include <thread>

#include "bridge/call_engine.h"
#include "bridge/handle_registry.h"
#include "bridge/type_converter.h"
#include "core/message_router.h"
#include "core/thread_sync.h"
#include "imgui.h"
#include "ui/widgets/object_builder_dialog.h"

namespace yamgg {

namespace {

// Same classification as ObjectBuilderDialog: "simple" args get a
// text field, "complex" args get [create…].
bool is_simple_type(const std::string& typeName) {
    using bridge::TypeConverter;
    using bridge::JavaKind;
    JavaKind k = TypeConverter::classify(typeName);
    switch (k) {
        case JavaKind::Unknown: {
            if (typeName.find("java.util.") == 0) return true;
            if (typeName.find("android.os.") == 0) return true;
            if (typeName.find("org.json.") == 0)  return true;
            return false;
        }
        case JavaKind::Object:
        case JavaKind::Parcelable:
        case JavaKind::Serializable:
            return false;
        default:
            return true;
    }
}

std::string clean_text(const std::string& s) {
    std::string r = s;
    while (!r.empty() && (r.back() == '\0' || r.back() == ' ' || r.back() == '\n')) {
        r.pop_back();
    }
    return r;
}

} // namespace

CallPanelTab& CallPanelTab::instance() {
    static CallPanelTab t;
    return t;
}

char* CallPanelTab::this_handle_buf_for_ui() {
    return this_handle_buf_;
}

void CallPanelTab::set_instance_created_callback(InstanceCreatedCallback cb) {
    instance_created_cb_ = std::move(cb);
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
    arg_handles_.assign(argument_types_.size(), std::string());

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

    // Build args JSON. For each arg: if it has a handle, use JavaObject;
    // else encode from text.
    nlohmann::json args_json = nlohmann::json::array();
    std::string error;
    for (size_t i = 0; i < argument_types_.size(); i++) {
        if (!arg_handles_[i].empty()) {
            args_json.push_back(nlohmann::json{
                {"__type", "JavaObject"},
                {"handlePtr", arg_handles_[i]}
            });
            continue;
        }
        std::string text = clean_text(arg_text_[i]);
        std::string enc_err;
        nlohmann::json enc = bridge::TypeConverter::encode_user_input(
            argument_types_[i], text, enc_err);
        if (!enc_err.empty()) {
            result_ok_    = false;
            result_error_ = "arg " + std::to_string(i) + ": " + enc_err;
            has_result_   = true;
            return;
        }
        args_json.push_back(enc);
    }

    in_flight_  = true;
    started_    = std::chrono::steady_clock::now();
    has_result_ = false;

    std::string cls    = class_buf_;
    std::string mth    = method_buf_;
    std::string handle = this_handle_buf_;
    bool is_static     = is_static_ || is_ctor_;

    std::thread([this, cls, mth, handle, is_static, args_json]() {
        nlohmann::json result;
        std::string err;
        bool ok = false;

        if (mth == "$new") {
            std::string new_handle;
            ok = bridge::CallEngine::instance().create_instance(
                cls, argument_types_, args_json, new_handle, err, 15000);
            if (ok) result = nlohmann::json{{"handle", new_handle}};
        } else if (is_static) {
            ok = bridge::CallEngine::instance().call_static(
                cls, mth, argument_types_, args_json, result, err, 15000);
        } else {
            if (handle.empty()) {
                err = "thisHandle required for instance method";
                ok  = false;
            } else {
                ok = bridge::CallEngine::instance().call_instance(
                    cls, mth, argument_types_, handle, args_json,
                    result, err, 15000);
            }
        }

        marshal_to_ui([this, ok, result, err]() {
            in_flight_  = false;
            result_ok_  = ok;
            has_result_ = true;
            if (ok) {
                result_value_ = result;
                result_error_.clear();
                if (result.is_object() && result.contains("handle") &&
                    result["handle"].is_string()) {
                    last_handle_ = result["handle"].get<std::string>();
                    bridge::HandleRegistry::instance().register_handle(
                        last_handle_, class_name_);
                    if (instance_created_cb_) {
                        auto cb = instance_created_cb_;
                        std::string cls_copy = class_name_;
                        std::string h_copy   = last_handle_;
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

// ═══════════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════════

void CallPanelTab::draw() {
    check_timeout();
    ObjectBuilderDialog::instance().draw();

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
    ImGui::InputTextWithHint("##mth", "Method (or $new)", method_buf_,
                             sizeof(method_buf_));

    ImGui::Checkbox("Static", &is_static_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##thishandle",
                             "thisHandle (required for instance methods)",
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
        const std::string& type = argument_types_[i];
        bool simple = is_simple_type(type);
        bool has_handle = !arg_handles_[i].empty();

        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                           "[%zu] %s", i, type.c_str());

        if (!simple) {
            ImGui::SameLine();
            if (has_handle) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                                   "✓ handle=%s…",
                                   arg_handles_[i].substr(0, 12).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("reset")) arg_handles_[i].clear();
            } else {
                if (ImGui::SmallButton("create…")) {
                    int arg_index = (int)i;
                    ObjectBuilderDialog::instance().open(type,
                        [this, arg_index](bool ok, const std::string& handle,
                                          const std::string& /*cls*/) {
                            if (!ok || arg_index < 0 ||
                                arg_index >= (int)arg_handles_.size()) {
                                return;
                            }
                            arg_handles_[arg_index] = handle;
                            arg_text_[arg_index].clear();
                        });
                }
            }
        } else {
            ImGui::SetNextItemWidth(-1);
            char buf[8192];
            std::snprintf(buf, sizeof(buf), "%s", arg_text_[i].c_str());
            if (ImGui::InputText("##val", buf, sizeof(buf))) {
                arg_text_[i] = buf;
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    if (in_flight_) {
        ImGui::TextDisabled("Invoking…");
    } else {
        if (ImGui::Button("Invoke", ImVec2(140.0f, 0.0f))) invoke();
    }
}

void CallPanelTab::draw_result_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Result");

    if (!has_result_) {
        ImGui::TextDisabled("(no result yet)");
        return;
    }
    if (!result_ok_) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "Error: %s", result_error_.c_str());
        return;
    }

    std::string display;
    try { display = bridge::TypeConverter::display(result_value_); }
    catch (...) { display = "<unprintable>"; }

    ImGui::TextWrapped("%s", display.c_str());
}

void CallPanelTab::draw_handles_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                       "Retained handles (%zu)",
                       bridge::HandleRegistry::instance().count());

    if (ImGui::Button("Release all")) release_all_handles();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##handlefilter", "Filter handles…",
                             handle_filter_buf_, sizeof(handle_filter_buf_));

    if (!last_handle_.empty()) {
        ImGui::Text("last: %s", last_handle_.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("release")) release_handle(last_handle_);
    }
}

void CallPanelTab::release_handle(const std::string& handle) {
    if (handle.empty()) return;
    std::string error;
    bridge::HandleRegistry::instance().release(handle, error, 4000);
    if (last_handle_ == handle) last_handle_.clear();
}

void CallPanelTab::release_all_handles() {
    bridge::HandleRegistry::instance().release_all();
    handles_cache_.clear();
    last_handle_.clear();
}

} // namespace yamgg
