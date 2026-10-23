// ui/tabs/class_browser_tab.cpp
#include "ui/tabs/class_browser_tab.h"

#include <cstdio>
#include <cstring>

#include "art/heap_scanner.h"
#include "bridge/handle_registry.h"
#include "bridge/hook_manager.h"
#include "bridge/type_converter.h"
#include "core/message_router.h"
#include "core/thread_sync.h"
#include "imgui.h"
#include "ui/main_window.h"
#include "ui/notification.h"
#include "ui/tabs/call_panel_tab.h"
#include "ui/tabs/hook_editor_tab.h"

namespace yamgg {

namespace {

constexpr int kTimeoutSeconds = 15;

std::string make_method_key(const std::string& cls,
                            const std::string& mth,
                            const std::vector<std::string>& args) {
    std::string key = cls + "|" + mth + "|";
    for (size_t i = 0; i < args.size(); i++) {
        if (i) key += ",";
        key += args[i];
    }
    return key;
}

} // namespace

ClassBrowserTab& ClassBrowserTab::instance() {
    static ClassBrowserTab t;
    return t;
}

void ClassBrowserTab::on_activated() {
    if (!initialized_) {
        initialized_ = true;

        // Register the instance-created callback once. When the call
        // panel creates an instance, we append it to our list so it
        // becomes selectable for instance method calls.
        static bool cb_registered = false;
        if (!cb_registered) {
            cb_registered = true;
            CallPanelTab::instance().set_instance_created_callback(
                [this](const std::string& cls, const std::string& handle) {
                    if (cls == detail_.class_name) {
                        instances_.push_back({handle, "<created>"});
                        selected_instance_ = (int)instances_.size() - 1;
                    }
                });
        }

        refresh_list();
    }
}

void ClassBrowserTab::show_class(const std::string& class_name) {
    if (class_name.empty()) return;
    tree_.set_selected(class_name);
    load_detail(class_name);
}

// ═══════════════════════════════════════════════════════════════
//  Loading
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::refresh_list() {
    if (list_.loading) return;
    list_.loading = true;
    list_.error.clear();
    list_.started = std::chrono::steady_clock::now();
    last_filter_ = filter_buf_;

    MessageRouter::instance().post_command(
        "bridge.class.list",
        nlohmann::json{{"filter", std::string(filter_buf_)}},
        [this](bool ok, const nlohmann::json& reply) {
            marshal_to_ui([this, ok, reply]() {
                list_.loading = false;
                if (!ok) {
                    list_.error = reply.value("error", std::string("unknown"));
                    return;
                }
                std::vector<std::string> classes;
                if (reply.contains("classes") && reply["classes"].is_array()) {
                    classes.reserve(reply["classes"].size());
                    for (const auto& c : reply["classes"]) {
                        if (c.is_string()) classes.push_back(c.get<std::string>());
                    }
                }
                tree_.set_data(classes);
            });
        });
}

void ClassBrowserTab::load_detail(const std::string& class_name) {
    if (class_name.empty()) return;
    if (detail_.loading && detail_.class_name == class_name) return;

    detail_.loading = true;
    detail_.class_name = class_name;
    detail_.error.clear();
    detail_.inspection = bridge::ClassInspection{};
    detail_.started = std::chrono::steady_clock::now();
    selected_class_ = class_name;

    // Reset per-class instance state.
    instances_.clear();
    selected_instance_ = -1;

    MessageRouter::instance().post_command(
        "bridge.class.inspect",
        nlohmann::json{{"className", class_name}},
        [this, class_name](bool ok, const nlohmann::json& reply) {
            marshal_to_ui([this, class_name, ok, reply]() {
                if (detail_.class_name != class_name) return;
                detail_.loading = false;
                if (!ok) {
                    detail_.error = reply.value("error", std::string("unknown"));
                    return;
                }
                detail_.inspection.className = reply.value("className", class_name);

                if (reply.contains("methods") && reply["methods"].is_array()) {
                    for (const auto& m : reply["methods"]) {
                        bridge::MethodSignature ms;
                        ms.name       = m.value("name", std::string());
                        ms.returnType = m.value("returnType", std::string());
                        ms.type       = m.value("type", std::string());
                        if (m.contains("argumentTypes") && m["argumentTypes"].is_array()) {
                            for (const auto& a : m["argumentTypes"]) {
                                if (a.is_string())
                                    ms.argumentTypes.push_back(a.get<std::string>());
                            }
                        }
                        detail_.inspection.methods.push_back(std::move(ms));
                    }
                }
                if (reply.contains("fields") && reply["fields"].is_array()) {
                    for (const auto& f : reply["fields"]) {
                        bridge::FieldSignature fs;
                        fs.name = f.value("name", std::string());
                        fs.type = f.value("type", std::string());
                        detail_.inspection.fields.push_back(std::move(fs));
                    }
                }
                if (reply.contains("constructors") && reply["constructors"].is_array()) {
                    for (const auto& c : reply["constructors"]) {
                        bridge::ConstructorSignature cs;
                        if (c.contains("argumentTypes") && c["argumentTypes"].is_array()) {
                            for (const auto& a : c["argumentTypes"]) {
                                if (a.is_string())
                                    cs.argumentTypes.push_back(a.get<std::string>());
                            }
                        }
                        detail_.inspection.constructors.push_back(std::move(cs));
                    }
                }
            });
        });
}

void ClassBrowserTab::check_timeouts() {
    auto now = std::chrono::steady_clock::now();
    if (list_.loading &&
        now - list_.started > std::chrono::seconds(kTimeoutSeconds)) {
        list_.loading = false;
        list_.error   = "timeout";
    }
    if (detail_.loading &&
        now - detail_.started > std::chrono::seconds(kTimeoutSeconds)) {
        detail_.loading = false;
        detail_.error   = "timeout";
    }
    if (method_search_.running &&
        now - method_search_.started > std::chrono::seconds(kTimeoutSeconds)) {
        method_search_.running = false;
        method_search_.error   = "timeout";
    }
}

// ═══════════════════════════════════════════════════════════════
//  Top-level draw
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::draw() {
    check_timeouts();

    if (std::strcmp(filter_buf_, last_filter_.c_str()) != 0 && !list_.loading) {
        refresh_list();
    }

    draw_method_search_bar();

    // ── Split: tree | details ──
    float avail  = ImGui::GetContentRegionAvail().x;
    float left_w = avail * 0.42f;
    if (left_w < 240.0f)         left_w = 240.0f;
    if (left_w > avail - 300.0f) left_w = avail - 300.0f;

    ImGui::BeginChild("##cb_left", ImVec2(left_w, 0), ImGuiChildFlags_None);
    {
        // Filter input lives inside the left panel.
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(
            "##filter",
            "substring | *wildcard* | /regex/i",
            filter_buf_, sizeof(filter_buf_));
        if (ImGui::Button("Refresh")) refresh_list();
        if (list_.loading) { ImGui::SameLine(); ImGui::TextDisabled("loading…"); }
        else if (!list_.error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                               "err: %s", list_.error.c_str());
        }

        tree_.set_filter(filter_buf_);
        bool changed = tree_.draw("##cb_tree", 0.0f);
        if (changed) {
            const std::string& sel = tree_.selected();
            if (!sel.empty() && sel != selected_class_) {
                load_detail(sel);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##cb_right", ImVec2(0, 0), ImGuiChildFlags_None);
    draw_details();
    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════
//  Details panel
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::draw_details() {
    if (detail_.class_name.empty()) {
        ImGui::TextDisabled("Select a class to inspect.");
        return;
    }

    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "%s",
                       detail_.class_name.c_str());

    if (detail_.loading) { ImGui::TextDisabled("loading…"); return; }
    if (!detail_.error.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                           "error: %s", detail_.error.c_str());
        return;
    }

    const auto& insp = detail_.inspection;

    // ── Instances section first (users need one before instance calls) ──
    draw_instances_section();

    // ── Constructors ──
    if (!insp.constructors.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                           "Constructors (%zu)", insp.constructors.size());
        for (size_t i = 0; i < insp.constructors.size(); i++) {
            const auto& c = insp.constructors[i];
            ImGui::PushID((int)i);
            ImGui::TextUnformatted("$new(");
            ImGui::SameLine();
            for (size_t j = 0; j < c.argumentTypes.size(); j++) {
                if (j) { ImGui::SameLine(); ImGui::TextUnformatted(", "); ImGui::SameLine(); }
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "%s", c.argumentTypes[j].c_str());
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(")");
            ImGui::SameLine();
            if (ImGui::SmallButton("create")) create_instance(c);
            ImGui::PopID();
        }
    }

    // ── Methods (main focus) ──
    if (!insp.methods.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                           "Methods (%zu)", insp.methods.size());

        ImGui::BeginChild("##methods", ImVec2(0, 380), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < insp.methods.size(); i++) {
            const auto& m = insp.methods[i];
            ImGui::PushID((int)i);

            std::string key = make_method_key(detail_.class_name, m.name,
                                              m.argumentTypes);
            MethodHookState& state = method_hooks_[key];
            bool hooked = state.active;

            // Colored signature while traced.
            if (hooked) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.78f, 0.30f, 1.0f));
            }
            ImGui::Text("%s", m.returnType.c_str());
            ImGui::SameLine();
            ImGui::Text("%s(", m.name.c_str());
            ImGui::SameLine();
            for (size_t j = 0; j < m.argumentTypes.size(); j++) {
                if (j) { ImGui::SameLine(); ImGui::TextUnformatted(", "); ImGui::SameLine(); }
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   "%s", m.argumentTypes[j].c_str());
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(")");
            if (hooked) ImGui::PopStyleColor();

            if (state.call_count > 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f),
                                   "(%u)", state.call_count);
            }

            ImGui::SameLine();
            if (hooked) {
                if (ImGui::SmallButton("untrace")) {
                    std::string err;
                    if (bridge::HookManager::instance().uninstall(
                            state.hook_id, err, 3000)) {
                        state.active    = false;
                        state.hook_id   = 0;
                    } else {
                        Notification::instance().push(
                            "untrace failed: " + err, 4.0f);
                    }
                }
            } else {
                if (ImGui::SmallButton("trace")) toggle_trace(m);
            }

            bool is_static = (m.type == "static");
            bool can_call  = is_static || (selected_instance_ >= 0);
            ImGui::SameLine();
            if (!can_call) ImGui::BeginDisabled();
            if (ImGui::SmallButton("call")) call_method(m);
            if (!can_call) ImGui::EndDisabled();

            if (!can_call) {
                ImGui::SameLine();
                ImGui::TextDisabled("(select an instance first)");
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // ── Fields ──
    if (!insp.fields.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                           "Fields (%zu)", insp.fields.size());
        for (size_t i = 0; i < insp.fields.size(); i++) {
            const auto& f = insp.fields[i];
            ImGui::PushID((int)i);
            ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                               "%s", f.type.c_str());
            ImGui::SameLine();
            ImGui::Text("%s", f.name.c_str());
            ImGui::PopID();
        }
    }

    if (insp.methods.empty() && insp.fields.empty() &&
        insp.constructors.empty()) {
        ImGui::TextDisabled("(no members visible via Java.use)");
    }
}

// ═══════════════════════════════════════════════════════════════
//  Instance acquisition
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::draw_instances_section() {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                       "Instances (%zu)", instances_.size());

    const char* strategies[] = { "Auto", "JVMTI", "GetInstances" };
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("##strategy", &instance_search_strategy_,
                 strategies, IM_ARRAYSIZE(strategies));
    ImGui::SameLine();

    if (instance_search_running_) {
        ImGui::TextDisabled("searching…");
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) stop_instance_search();
    } else {
        if (ImGui::SmallButton("Find instances")) find_instances();
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            instances_.clear();
            selected_instance_ = -1;
            instance_search_error_.clear();
        }
    }

    if (!instance_search_error_.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                           "error: %s", instance_search_error_.c_str());
    }

    if (!instances_.empty()) {
        ImGui::BeginChild("##instances", ImVec2(0, 140), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < instances_.size(); i++) {
            const auto& inst = instances_[i];
            ImGui::PushID((int)i);
            bool is_selected = (selected_instance_ == (int)i);
            if (ImGui::RadioButton("##sel", is_selected)) {
                selected_instance_ = (int)i;
            }
            ImGui::SameLine();
            std::string label = inst.display.empty()
                ? ("<" + detail_.class_name + ">")
                : inst.display;
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                selected_instance_ = (int)i;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("handle=%s…",
                                inst.handle.substr(0, 12).c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

void ClassBrowserTab::find_instances() {
    if (instance_search_running_ || detail_.class_name.empty()) return;

    instance_search_running_ = true;
    instance_search_error_.clear();
    instances_.clear();
    selected_instance_ = -1;

    art::ChooseStrategy strategy = art::ChooseStrategy::Auto;
    if (instance_search_strategy_ == 1) strategy = art::ChooseStrategy::Jvmti;
    if (instance_search_strategy_ == 2) strategy = art::ChooseStrategy::Jvmti;

    std::string error;
    uint64_t id = art::HeapScanner::instance().choose(
        detail_.class_name,
        [this](const art::HeapMatch& m) {
            marshal_to_ui([this, m]() {
                instances_.push_back({m.handle, m.stringRep});
            });
        },
        [this](uint64_t, bool) {
            marshal_to_ui([this]() { instance_search_running_ = false; });
        },
        error,
        5000,
        strategy);

    if (id == 0) {
        instance_search_running_ = false;
        instance_search_error_ = error;
    } else {
        instance_search_id_ = id;
    }
}

void ClassBrowserTab::stop_instance_search() {
    if (!instance_search_running_) return;
    if (instance_search_id_ != 0) {
        art::HeapScanner::instance().cancel(instance_search_id_);
        instance_search_id_ = 0;
    }
    instance_search_running_ = false;
}

// ═══════════════════════════════════════════════════════════════
//  Trace toggle
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::toggle_trace(const bridge::MethodSignature& m) {
    std::string key = make_method_key(detail_.class_name, m.name, m.argumentTypes);
    auto& state = method_hooks_[key];
    if (state.active) return;

    std::string error;
    uint64_t hook_id = bridge::HookManager::instance().install(
        detail_.class_name, m.name, m.argumentTypes,
        // Per-hook enter callback: increments this method's counter.
        [this, key](const bridge::HookEnterEvent&) {
            marshal_to_ui([this, key]() {
                auto it = method_hooks_.find(key);
                if (it != method_hooks_.end()) {
                    it->second.call_count++;
                }
            });
        },
        nullptr,
        error,
        5000);

    if (hook_id == 0) {
        Notification::instance().push("trace failed: " + error, 4.0f);
        return;
    }
    state.hook_id    = hook_id;
    state.active     = true;
    state.call_count = 0;
}

// ═══════════════════════════════════════════════════════════════
//  Call / create
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::call_method(const bridge::MethodSignature& m) {
    std::string this_handle;
    if (m.type != "static" && selected_instance_ >= 0) {
        this_handle = instances_[selected_instance_].handle;
    }
    CallPanelTab::instance().prepare_call(
        detail_.class_name, m.name, m.argumentTypes,
        m.type == "static", this_handle);
    MainWindow::instance().select_tab(2);
}

void ClassBrowserTab::create_instance(const bridge::ConstructorSignature& ctor) {
    CallPanelTab::instance().prepare_call(
        detail_.class_name, "$new", ctor.argumentTypes,
        false, "");
    MainWindow::instance().select_tab(2);
}

// ═══════════════════════════════════════════════════════════════
//  Global method search
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::draw_method_search_bar() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                       "Global method search");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220.0f);
    ImGui::InputTextWithHint("##msearch",
                             "com.example.*!onCreate   or   *!*",
                             method_pattern_buf_,
                             sizeof(method_pattern_buf_));
    ImGui::SameLine();
    if (method_search_.running) {
        ImGui::TextDisabled("searching…");
    } else {
        if (ImGui::Button("Search")) start_method_search();
    }

    if (!method_search_.error.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                           "error: %s", method_search_.error.c_str());
    }

    if (!method_search_.matches.empty()) {
        if (ImGui::CollapsingHeader("Search results")) {
            ImGui::BeginChild("##mresults", ImVec2(0, 160),
                              ImGuiChildFlags_Borders);
            for (size_t i = 0; i < method_search_.matches.size(); i++) {
                const auto& m = method_search_.matches[i];
                ImGui::PushID((int)i);
                std::string label = m.className + "." + m.methodName;
                if (ImGui::Selectable(label.c_str())) show_class(m.className);
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }

    ImGui::Separator();
}

void ClassBrowserTab::start_method_search() {
    if (method_search_.running) return;
    method_search_.running  = true;
    method_search_.error.clear();
    method_search_.matches.clear();
    method_search_.pattern  = method_pattern_buf_;
    method_search_.started  = std::chrono::steady_clock::now();

    MessageRouter::instance().post_command(
        "bridge.class.enumerate_methods",
        nlohmann::json{{"pattern", method_search_.pattern}},
        [this](bool ok, const nlohmann::json& reply) {
            marshal_to_ui([this, ok, reply]() {
                method_search_.running = false;
                if (!ok) {
                    method_search_.error = reply.value("error", std::string("unknown"));
                    return;
                }
                if (reply.contains("matches") && reply["matches"].is_array()) {
                    for (const auto& m : reply["matches"]) {
                        MethodSearchState::Match rec;
                        rec.className  = m.value("className",  std::string());
                        rec.methodName = m.value("methodName", std::string());
                        method_search_.matches.push_back(std::move(rec));
                    }
                }
            });
        });
}

} // namespace yamgg
