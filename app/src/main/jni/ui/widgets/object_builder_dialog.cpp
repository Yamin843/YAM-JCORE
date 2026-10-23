// ui/widgets/object_builder_dialog.cpp
#include "ui/widgets/object_builder_dialog.h"

#include <cstdio>
#include <cstring>

#include <json.hpp>

#include "bridge/handle_registry.h"
#include "bridge/type_converter.h"
#include "core/message_router.h"
#include "core/thread_sync.h"
#include "imgui.h"

namespace yamgg {

namespace {

constexpr int kTimeoutSeconds = 20;

// Returns true when the type can be entered as text (primitive,
// String, arrays, List, Map, Bundle, JSON*, etc.). Returns false for
// arbitrary Java objects that need a nested builder.
bool is_simple_type(const std::string& typeName) {
    using bridge::TypeConverter;
    using bridge::JavaKind;
    JavaKind k = TypeConverter::classify(typeName);

    switch (k) {
        case JavaKind::Unknown: {
            // Unknown package names are "simple" if they look like a
            // standard container; else treat as complex.
            if (typeName.find("java.util.") == 0) return true;
            if (typeName.find("android.os.") == 0) return true;
            if (typeName.find("org.json.") == 0)  return true;
            return false;
        }
        case JavaKind::Void:      return true;
        case JavaKind::Object:    return false;  // Object could be anything; assume complex
        case JavaKind::Parcelable:
        case JavaKind::Serializable:
            return false;
        default:
            return true;
    }
}

// Trim trailing whitespace and any leftover NUL bytes from a text buffer.
std::string clean_text(const std::string& s) {
    std::string r = s;
    while (!r.empty() && (r.back() == '\0' || r.back() == ' ' || r.back() == '\n')) {
        r.pop_back();
    }
    return r;
}

} // namespace

ObjectBuilderDialog& ObjectBuilderDialog::instance() {
    static ObjectBuilderDialog d;
    return d;
}

bool ObjectBuilderDialog::is_open() const {
    return open_;
}

void ObjectBuilderDialog::open(const std::string& className, Callback cb) {
    stack_.clear();
    cb_       = std::move(cb);
    open_     = true;
    in_flight_ = false;
    error_.clear();

    Frame f;
    f.className = className;
    f.loading   = true;
    f.started   = std::chrono::steady_clock::now();
    stack_.push_back(std::move(f));
    load_frame(0);
}

void ObjectBuilderDialog::close() {
    open_      = false;
    in_flight_ = false;
    stack_.clear();
    cb_        = nullptr;
    error_.clear();
}

void ObjectBuilderDialog::load_frame(int frame_index) {
    if (frame_index < 0 || frame_index >= (int)stack_.size()) return;
    Frame& f = stack_[frame_index];

    f.loading = true;
    f.error.clear();
    f.started = std::chrono::steady_clock::now();

    std::string className = f.className;
    MessageRouter::instance().post_command(
        "bridge.class.inspect",
        nlohmann::json{{"className", className}, {"forceRefresh", true}},
        [this, frame_index, className](bool ok, const nlohmann::json& reply) {
            marshal_to_ui([this, frame_index, className, ok, reply]() {
                if (frame_index >= (int)stack_.size()) return;
                Frame& fr = stack_[frame_index];
                if (fr.className != className) return;

                fr.loading = false;
                fr.loaded  = true;
                if (!ok) {
                    fr.error = reply.value("error", std::string("unknown"));
                    return;
                }
                fr.inspection.className = reply.value("className", className);
                if (reply.contains("constructors") && reply["constructors"].is_array()) {
                    for (const auto& c : reply["constructors"]) {
                        bridge::ConstructorSignature cs;
                        if (c.contains("argumentTypes") && c["argumentTypes"].is_array()) {
                            for (const auto& a : c["argumentTypes"]) {
                                if (a.is_string())
                                    cs.argumentTypes.push_back(a.get<std::string>());
                            }
                        }
                        fr.inspection.constructors.push_back(std::move(cs));
                    }
                }
                fr.selected_ctor = 0;
                if (!fr.inspection.constructors.empty()) {
                    const auto& ctor = fr.inspection.constructors[0];
                    fr.arg_types.assign(ctor.argumentTypes.begin(),
                                        ctor.argumentTypes.end());
                    fr.arg_text.assign(ctor.argumentTypes.size(), std::string());
                    fr.arg_handles.assign(ctor.argumentTypes.size(), std::string());
                }
            });
        });
}

void ObjectBuilderDialog::check_timeouts() {
    auto now = std::chrono::steady_clock::now();
    for (auto& f : stack_) {
        if (f.loading && now - f.started > std::chrono::seconds(kTimeoutSeconds)) {
            f.loading = false;
            f.loaded  = true;
            f.error   = "timeout";
        }
    }
    if (in_flight_ && now - in_flight_start_ > std::chrono::seconds(kTimeoutSeconds)) {
        in_flight_ = false;
        error_     = "create timeout";
    }
}

void ObjectBuilderDialog::open_nested(int arg_index) {
    if (stack_.empty()) return;
    Frame& parent = stack_.back();
    if (arg_index < 0 || arg_index >= (int)parent.arg_types.size()) return;
    if (!parent.arg_handles[arg_index].empty()) return;

    Frame child;
    child.className        = parent.arg_types[arg_index];
    child.loading          = true;
    child.started          = std::chrono::steady_clock::now();
    child.parent_arg_index = arg_index;
    stack_.push_back(std::move(child));
    load_frame((int)stack_.size() - 1);
}

void ObjectBuilderDialog::try_create() {
    if (stack_.empty() || in_flight_) return;
    Frame& f = stack_.back();
    if (!f.loaded || f.inspection.constructors.empty()) return;

    // Build args JSON.
    nlohmann::json args = nlohmann::json::array();
    std::string error;
    for (size_t i = 0; i < f.arg_types.size(); i++) {
        if (!f.arg_handles[i].empty()) {
            // Complex arg: send as JavaObject.
            args.push_back(nlohmann::json{
                {"__type", "JavaObject"},
                {"handlePtr", f.arg_handles[i]}
            });
            continue;
        }
        std::string text = clean_text(f.arg_text[i]);
        std::string enc_err;
        nlohmann::json encoded = bridge::TypeConverter::encode_user_input(
            f.arg_types[i], text, enc_err);
        if (!enc_err.empty()) {
            error = "arg " + std::to_string(i) + ": " + enc_err;
            break;
        }
        args.push_back(encoded);
    }

    if (!error.empty()) {
        error_ = error;
        return;
    }

    nlohmann::json req = {
        {"className", f.className},
        {"ctorTypes", f.arg_types},
        {"args",      args}
    };

    in_flight_       = true;
    in_flight_start_ = std::chrono::steady_clock::now();
    error_.clear();

    int frame_index = (int)stack_.size() - 1;
    std::string className = f.className;

    MessageRouter::instance().post_command(
        "bridge.call.create", req,
        [this, frame_index, className](bool ok, const nlohmann::json& reply) {
            marshal_to_ui([this, frame_index, className, ok, reply]() {
                in_flight_ = false;
                if (!ok) {
                    error_ = reply.value("error", std::string("create failed"));
                    return;
                }
                std::string handle = reply.value("handle", std::string());
                if (handle.empty()) {
                    error_ = "create returned empty handle";
                    return;
                }
                bridge::HandleRegistry::instance().register_handle(handle, className);

                // If this is the outermost frame, fire the user callback.
                if (frame_index == 0) {
                    auto cb = cb_;
                    close();
                    if (cb) cb(true, handle, className);
                    return;
                }

                // Nested: store in parent and pop.
                int parent_index = frame_index - 1;
                if (parent_index >= 0 && parent_index < (int)stack_.size()) {
                    Frame& parent = stack_[parent_index];
                    int ai = stack_[frame_index].parent_arg_index;
                    if (ai >= 0 && ai < (int)parent.arg_handles.size()) {
                        parent.arg_handles[ai] = handle;
                        parent.arg_text[ai].clear();
                    }
                }
                stack_.pop_back();
            });
        });
}

void ObjectBuilderDialog::draw() {
    if (!open_) return;

    check_timeouts();

    if (stack_.empty()) { close(); return; }

    Frame& f = stack_.back();

    char title[512];
    std::snprintf(title, sizeof(title), "Build %s###object_builder",
                  f.className.c_str());

    ImGui::OpenPopup(title);
    ImVec2 vp = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(vp.x * 0.6f, vp.y * 0.7f),
                             ImGuiCond_Appearing);

    bool still_open = true;
    if (ImGui::BeginPopupModal(title, &still_open,
            ImGuiWindowFlags_NoSavedSettings)) {

        // Breadcrumb.
        ImGui::TextDisabled("Building:");
        ImGui::SameLine();
        for (size_t i = 0; i < stack_.size(); i++) {
            if (i) { ImGui::SameLine(); ImGui::TextUnformatted(" → "); ImGui::SameLine(); }
            ImVec4 c = (i + 1 == stack_.size())
                ? ImVec4(0.94f, 0.78f, 0.30f, 1.0f)
                : ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
            ImGui::TextColored(c, "%s", stack_[i].className.c_str());
        }

        ImGui::Separator();

        if (f.loading) {
            ImGui::TextDisabled("loading class…");
        } else if (!f.error.empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                               "error: %s", f.error.c_str());
        } else if (f.inspection.constructors.empty()) {
            ImGui::TextDisabled(
                "No constructors visible. This class can only be produced "
                "from an existing instance handle (paste one below).");
        } else {
            // Constructor picker.
            if (f.inspection.constructors.size() > 1) {
                ImGui::TextDisabled("Constructor:");
                for (size_t i = 0; i < f.inspection.constructors.size(); i++) {
                    const auto& c = f.inspection.constructors[i];
                    std::string label = "$new(";
                    for (size_t j = 0; j < c.argumentTypes.size(); j++) {
                        if (j) label += ", ";
                        label += c.argumentTypes[j];
                    }
                    label += ")";
                    ImGui::PushID((int)i);
                    if (ImGui::RadioButton(label.c_str(),
                            f.selected_ctor == (int)i)) {
                        f.selected_ctor = (int)i;
                        f.arg_types.assign(c.argumentTypes.begin(),
                                            c.argumentTypes.end());
                        f.arg_text.assign(c.argumentTypes.size(), std::string());
                        f.arg_handles.assign(c.argumentTypes.size(), std::string());
                    }
                    ImGui::PopID();
                }
                ImGui::Separator();
            }

            // Argument editors.
            ImGui::TextDisabled("Arguments:");
            ImGui::BeginChild("##args", ImVec2(0, 0),
                              ImGuiChildFlags_Borders);
            for (size_t i = 0; i < f.arg_types.size(); i++) {
                ImGui::PushID((int)i);
                const std::string& type = f.arg_types[i];
                bool simple = is_simple_type(type);
                bool has_handle = !f.arg_handles[i].empty();

                ImGui::Text("[%zu] %s", i, type.c_str());
                if (!simple) {
                    ImGui::SameLine();
                    if (has_handle) {
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1),
                                           "✓ handle=%s…",
                                           f.arg_handles[i].substr(0, 12).c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("reset")) {
                            f.arg_handles[i].clear();
                        }
                    } else {
                        if (ImGui::SmallButton("create…")) {
                            open_nested((int)i);
                            ImGui::PopID();
                            break;
                        }
                    }
                } else {
                    char buf[8192];
                    std::snprintf(buf, sizeof(buf), "%s", f.arg_text[i].c_str());
                    if (ImGui::InputText("##val", buf, sizeof(buf))) {
                        f.arg_text[i] = buf;
                    }
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                               "%s", error_.c_str());
        }

        ImGui::Separator();

        bool can_create = f.loaded && !f.loading && f.error.empty() &&
                          !f.inspection.constructors.empty() && !in_flight_;
        if (!can_create) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(140, 0))) try_create();
        if (!can_create) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140, 0))) {
            if (stack_.size() > 1) {
                stack_.pop_back();
            } else {
                auto cb = cb_;
                close();
                if (cb) cb(false, "", "");
            }
        }

        ImGui::EndPopup();
    }

    if (!still_open) {
        // User closed via X.
        auto cb = cb_;
        close();
        if (cb) cb(false, "", "");
    }
}

} // namespace yamgg
