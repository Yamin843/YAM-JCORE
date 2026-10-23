// ui/tabs/class_browser_tab.cpp
#include "ui/tabs/class_browser_tab.h"

#include <cstring>

#include "bridge/type_converter.h"
#include "core/message_router.h"
#include "core/thread_sync.h"
#include "imgui.h"
#include "ui/main_window.h"
#include "ui/tabs/call_panel_tab.h"
#include "ui/tabs/hook_editor_tab.h"
#include "art/heap_scanner.h"

namespace yamgg {

namespace {
constexpr int kTimeoutSeconds = 15;
}

ClassBrowserTab& ClassBrowserTab::instance() {
    static ClassBrowserTab t;
    return t;
}

void ClassBrowserTab::on_activated() {
    if (!initialized_) {
        initialized_ = true;
        refresh_list();
    }
}

void ClassBrowserTab::show_class(const std::string& class_name) {
    if (class_name.empty()) return;
    tree_.set_selected(class_name);
    load_detail(class_name);
}

// ═══════════════════════════════════════════════════════════════
//  Async loading
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::refresh_list() {
    if (list_.loading) return;
    list_.loading  = true;
    list_.error.clear();
    list_.started  = std::chrono::steady_clock::now();

    std::string filter = filter_buf_;
    last_filter_ = filter;

    MessageRouter::instance().post_command(
        "bridge.class.list",
        nlohmann::json{{"filter", filter}},
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

    detail_.loading     = true;
    detail_.class_name  = class_name;
    detail_.error.clear();
    detail_.inspection  = bridge::ClassInspection{};
    detail_.started     = std::chrono::steady_clock::now();
    selected_class_     = class_name;

    MessageRouter::instance().post_command(
        "bridge.class.inspect",
        nlohmann::json{{"className", class_name}},
        [this, class_name](bool ok, const nlohmann::json& reply) {
            marshal_to_ui([this, class_name, ok, reply]() {
                // Ignore stale replies for a class the user already switched away from.
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
}

// ═══════════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::draw() {
    check_timeouts();

    // Trigger refresh on filter change.
    if (std::strcmp(filter_buf_, last_filter_.c_str()) != 0 && !list_.loading) {
        refresh_list();
    }

    // ── Global method search bar ────────────────────────────
    draw_method_search_section();

    // ── Top toolbar ──
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::InputTextWithHint(
        "##filter",
        "Filter: substring, wildcard (*Bundle*), or regex (/^android\\./i)",
        filter_buf_, sizeof(filter_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Refresh") && !list_.loading) {
        refresh_list();
    }

    if (list_.loading) {
        ImGui::SameLine();
        ImGui::TextDisabled("loading…");
    } else if (!list_.error.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "error: %s",
                           list_.error.c_str());
    }

    ImGui::Separator();

    // ── Split: tree | details ──
    float avail = ImGui::GetContentRegionAvail().x;
    float left_w = avail * 0.42f;
    if (left_w < 240.0f) left_w = 240.0f;
    if (left_w > avail - 240.0f) left_w = avail - 240.0f;

    ImGui::BeginChild("##cb_left", ImVec2(left_w, 0), ImGuiChildFlags_None);
    {
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

void ClassBrowserTab::draw_details() {
    if (detail_.class_name.empty()) {
        ImGui::TextDisabled("Select a class to inspect.");
        return;
    }

    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "%s",
                       detail_.class_name.c_str());

    if (detail_.loading) {
        ImGui::TextDisabled("loading…");
        return;
    }
    if (!detail_.error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "error: %s",
                           detail_.error.c_str());
        return;
    }

    const auto& insp = detail_.inspection;

    // ── Constructors ──
    if (!insp.constructors.empty()) {
        if (ImGui::CollapsingHeader("Constructors",
                ImGuiTreeNodeFlags_DefaultOpen)) {
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
                if (ImGui::SmallButton("Call…")) {
                    CallPanelTab::instance().prepare_call(
                        detail_.class_name, "$new", c.argumentTypes, /*is_static=*/false);
                    MainWindow::instance().select_tab(2);
                }
                ImGui::PopID();
            }
        }
    }

    // ── Methods ──
    if (!insp.methods.empty()) {
        if (ImGui::CollapsingHeader("Methods",
                ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t i = 0; i < insp.methods.size(); i++) {
                const auto& m = insp.methods[i];
                ImGui::PushID((int)i);

                ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                                   "%s", m.returnType.c_str());
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

                ImGui::SameLine();
                if (ImGui::SmallButton("Hook")) {
                    HookEditorTab::instance().prepare_hook(
                        detail_.class_name, m.name, m.argumentTypes);
                    MainWindow::instance().select_tab(1);
                }
                ImGui::SameLine();
                bool is_static = (m.type == "static");
                if (ImGui::SmallButton("Call…")) {
                    CallPanelTab::instance().prepare_call(
                        detail_.class_name, m.name, m.argumentTypes, is_static);
                    MainWindow::instance().select_tab(2);
                }

                ImGui::PopID();
            }
        }
    }

    // ── Fields ──
    if (!insp.fields.empty()) {
        if (ImGui::CollapsingHeader("Fields",
                ImGuiTreeNodeFlags_DefaultOpen)) {
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
    }

    if (insp.constructors.empty() && insp.methods.empty() && insp.fields.empty()) {
        ImGui::TextDisabled("(no members visible via Java.use)");
    }

    // Instance search lives at the bottom of the details panel so it
    // always refers to the currently selected class.
    draw_instance_search_section();
}



// ═══════════════════════════════════════════════════════════════
//  Instance search (heap.choose via strategy)
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::start_instance_search() {
    if (detail_.class_name.empty() || instance_search_.running) return;

    instance_search_.running       = true;
    instance_search_.total_matches = 0;
    instance_search_.error.clear();
    instance_search_.matches.clear();
    instance_search_.started = std::chrono::steady_clock::now();

    art::ChooseStrategy strategy = art::ChooseStrategy::Auto;
    if (instance_search_.strategy == 1) strategy = art::ChooseStrategy::Jvmti;
    if (instance_search_.strategy == 2) strategy = art::ChooseStrategy::Jvmti;  // GetInstances falls back to JVMTI

    std::string error;
    uint64_t id = art::HeapScanner::instance().choose(
        detail_.class_name,
        [this](const art::HeapMatch& m) {
            marshal_to_ui([this, m]() {
                instance_search_.total_matches++;
                InstanceSearchState::Match rec;
                rec.handle  = m.handle;
                rec.display = m.stringRep;
                instance_search_.matches.push_back(std::move(rec));
            });
        },
        [this](uint64_t /*id*/, bool /*aborted*/) {
            marshal_to_ui([this]() {
                instance_search_.running = false;
            });
        },
        error,
        10000,
        strategy);

    if (id == 0) {
        instance_search_.running = false;
        instance_search_.error   = error;
    } else {
        instance_search_.session_id = id;
    }
}

void ClassBrowserTab::stop_instance_search() {
    if (!instance_search_.running || instance_search_.session_id == 0) return;
    art::HeapScanner::instance().cancel(instance_search_.session_id);
    instance_search_.session_id = 0;
}

void ClassBrowserTab::draw_instance_search_section() {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f),
                       "Instance search — %s", detail_.class_name.c_str());

    // Strategy dropdown.
    const char* strategies[] = { "Auto", "JVMTI", "GetInstances" };
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("##strategy", &instance_search_.strategy,
                 strategies, IM_ARRAYSIZE(strategies));

    ImGui::SameLine();

    if (instance_search_.running) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - instance_search_.started).count();
        ImGui::TextDisabled("searching… %d found, %llds",
                            instance_search_.total_matches, (long long)elapsed);
        ImGui::SameLine();
        if (ImGui::Button("Stop")) stop_instance_search();
    } else {
        if (ImGui::Button("Find instances")) start_instance_search();
    }

    if (!instance_search_.error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "error: %s",
                           instance_search_.error.c_str());
    }

    if (!instance_search_.matches.empty()) {
        ImGui::BeginChild("##instance_results",
                          ImVec2(0, 240), ImGuiChildFlags_Border);
        for (size_t i = 0; i < instance_search_.matches.size(); i++) {
            const auto& m = instance_search_.matches[i];
            ImGui::PushID((int)i);

            // Wrap in a selectable so the user can highlight one for
            // hooking/calling from the other tabs.
            std::string label = m.display.empty()
                ? m.handle
                : (m.display + "  [" + m.handle.substr(0, 12) + "…]");
            if (ImGui::Selectable(label.c_str())) {
                // Selecting a handle copies it to the "thisHandle" field
                // of CallPanelTab so the user can invoke methods on it.
                std::snprintf(CallPanelTab::instance().this_handle_buf_for_ui(),
                              sizeof(CallPanelTab::instance().this_handle_buf_for_ui()),
                              "%s", m.handle.c_str());
                MainWindow::instance().select_tab(2);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Release")) {
                std::string err;
                bridge::HandleRegistry::instance().release(m.handle, err, 2000);
                instance_search_.matches.erase(instance_search_.matches.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

// ═══════════════════════════════════════════════════════════════
//  Method search (Java.enumerateMethods)
// ═══════════════════════════════════════════════════════════════

void ClassBrowserTab::start_method_search() {
    if (method_search_.running) return;
    method_search_.running = true;
    method_search_.error.clear();
    method_search_.matches.clear();
    method_search_.pattern = method_pattern_buf_;
    method_search_.started = std::chrono::steady_clock::now();

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
                        rec.loader     = m.value("loader",     std::string());
                        method_search_.matches.push_back(std::move(rec));
                    }
                }
            });
        });
}

void ClassBrowserTab::draw_method_search_section() {
    ImGui::TextColored(ImVec4(0.94f, 0.78f, 0.30f, 1.0f), "Global method search");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220.0f);
    ImGui::InputTextWithHint("##method_pattern",
                             "pattern e.g. com.example.*!onCreate or *!*",
                             method_pattern_buf_, sizeof(method_pattern_buf_));
    ImGui::SameLine();
    if (method_search_.running) {
        ImGui::TextDisabled("searching…");
    } else {
        if (ImGui::Button("Search")) start_method_search();
    }

    if (!method_search_.error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "error: %s",
                           method_search_.error.c_str());
    }

    if (!method_search_.matches.empty()) {
        if (ImGui::CollapsingHeader("Search results")) {
            ImGui::BeginChild("##method_results", ImVec2(0, 180), ImGuiChildFlags_Border);
            for (size_t i = 0; i < method_search_.matches.size(); i++) {
                const auto& m = method_search_.matches[i];
                ImGui::PushID((int)i);
                std::string label = m.className + "." + m.methodName;
                if (ImGui::Selectable(label.c_str())) {
                    show_class(m.className);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }

    ImGui::Separator();
}

} // namespace yamgg
