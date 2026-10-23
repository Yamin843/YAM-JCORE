// ui/tabs/hook_editor_tab.cpp
#include "ui/tabs/hook_editor_tab.h"

#include <cstdio>
#include <cstring>

#include "core/thread_sync.h"
#include "imgui.h"

namespace yamgg {

HookEditorTab& HookEditorTab::instance() {
    static HookEditorTab t;
    return t;
}

HookEditorTab::HookEditorTab() {
    // Subscribe to hook events. Both callbacks arrive on the JS thread.
    bridge::HookManager::instance().set_global_enter_callback(
        [this](const bridge::HookEnterEvent& ev) { on_enter(ev); });
    bridge::HookManager::instance().set_global_leave_callback(
        [this](const bridge::HookLeaveEvent& ev) { on_leave(ev); });
}

void HookEditorTab::on_activated() {
    if (!initialized_) {
        initialized_ = true;
        refresh_hook_list();
    }
}

void HookEditorTab::prepare_hook(const std::string& class_name,
                                 const std::string& method_name,
                                 const std::vector<std::string>& overloads) {
    class_name_  = class_name;
    method_name_ = method_name;

    std::snprintf(class_buf_,  sizeof(class_buf_),  "%s", class_name.c_str());
    std::snprintf(method_buf_, sizeof(method_buf_), "%s", method_name.c_str());

    overload_signatures_.clear();
    for (const auto& t : overloads) {
        if (overload_signatures_.empty()) overload_signatures_.push_back(t);
        else overload_signatures_.push_back(t);
    }
    overload_buf_[0] = '\0';
    if (!overloads.empty()) {
        std::string sig;
        for (size_t i = 0; i < overloads.size(); i++) {
            if (i) sig += ",";
            sig += overloads[i];
        }
        std::snprintf(overload_buf_, sizeof(overload_buf_), "%s", sig.c_str());
    }
}

void HookEditorTab::refresh_hook_list() {
    hooks_cache_ = bridge::HookManager::instance().list();
}

void HookEditorTab::install_hook() {
    if (install_in_flight_) return;
    install_error_.clear();

    std::string cls = class_buf_;
    std::string mth = method_buf_;
    std::vector<std::string> overloads;
    if (overload_buf_[0] != '\0') {
        std::string s = overload_buf_;
        size_t start = 0;
        while (start < s.size()) {
            size_t comma = s.find(',', start);
            std::string tok = (comma == std::string::npos)
                ? s.substr(start) : s.substr(start, comma - start);
            // Trim whitespace.
            size_t b = 0, e = tok.size();
            while (b < e && (tok[b] == ' ' || tok[b] == '\t')) b++;
            while (e > b && (tok[e-1] == ' ' || tok[e-1] == '\t')) e--;
            if (b < e) overloads.push_back(tok.substr(b, e - b));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }

    install_in_flight_ = true;
    install_started_   = std::chrono::steady_clock::now();

    // Do the install on a worker thread so the UI never blocks.
    // The HookManager handles timeout; we just marshal the outcome.
    std::string error;
    uint64_t hook_id = bridge::HookManager::instance().install(
        cls, mth, overloads,
        /*onEnter=*/nullptr,   // global callback already subscribed
        /*onLeave=*/nullptr,
        error,
        8000);

    marshal_to_ui([this, hook_id, error]() {
        install_in_flight_ = false;
        if (hook_id == 0) {
            install_error_ = error.empty() ? "install failed" : error;
            return;
        }
        refresh_hook_list();
    });
}

void HookEditorTab::uninstall_hook(uint64_t hook_id) {
    std::string error;
    bool ok = bridge::HookManager::instance().uninstall(hook_id, error, 4000);
    if (!ok) {
        install_error_ = error.empty() ? "uninstall failed" : error;
        return;
    }
    refresh_hook_list();
}

void HookEditorTab::on_enter(const bridge::HookEnterEvent& ev) {
    std::string text = "ENTER " + ev.className + "." + ev.methodName + "(";
    if (ev.args.is_array()) {
        for (size_t i = 0; i < ev.args.size(); i++) {
            if (i) text += ", ";
            try { text += ev.args[i].dump(); } catch (...) { text += "?"; }
        }
    }
    text += ")";
    append_log(true, text);
}

void HookEditorTab::on_leave(const bridge::HookLeaveEvent& ev) {
    std::string text = "LEAVE " + ev.className + "." + ev.methodName + " → ";
    if (ev.threw) {
        text += "threw " + ev.error;
    } else {
        try { text += ev.returnValue.dump(); } catch (...) { text += "?"; }
    }
    append_log(false, text);
}

void HookEditorTab::append_log(bool enter, const std::string& text) {
    std::lock_guard<std::mutex> lk(log_mu_);
    LogEntry e;
    e.enter = enter;
    e.text  = text;
    e.when  = std::chrono::steady_clock::now();
    log_.push_back(std::move(e));
}

// ═══════════════════════════════════════════════════════════════
//  Draw
// ═══════════════════════════════════════════════════════════════

void HookEditorTab::draw() {
    // Check install timeout.
    if (install_in_flight_ &&
        std::chrono::steady_clock::now() - install_started_ >
        std::chrono::seconds(10)) {
        install_in_flight_ = false;
        install_error_ = "install timeout";
    }

    if (ImGui::BeginTabBar("##hk_bar")) {
        if (ImGui::BeginTabItem("Active")) {
            draw_active_hooks();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Install")) {
            draw_installer();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Log")) {
            draw_log();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void HookEditorTab::draw_active_hooks() {
    if (ImGui::Button("Refresh")) refresh_hook_list();
    ImGui::Separator();

    if (hooks_cache_.empty()) {
        ImGui::TextDisabled("No active hooks.");
        return;
    }

    if (ImGui::BeginTable("##hk_table", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_SizingStretchProp)) {

        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Class");
        ImGui::TableSetupColumn("Method");
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (const auto& h : hooks_cache_) {
            ImGui::TableNextRow();
            ImGui::PushID((int)h.hookId);

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu", (unsigned long long)h.hookId);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(h.className.c_str());

            ImGui::TableSetColumnIndex(2);
            std::string sig = h.methodName + "(";
            for (size_t i = 0; i < h.overloadTypes.size(); i++) {
                if (i) sig += ",";
                sig += h.overloadTypes[i];
            }
            sig += ")";
            ImGui::TextUnformatted(sig.c_str());

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Unhook")) {
                uninstall_hook(h.hookId);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void HookEditorTab::draw_installer() {
    ImGui::TextDisabled("Class name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##class", class_buf_, sizeof(class_buf_));

    ImGui::TextDisabled("Method name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##method", method_buf_, sizeof(method_buf_));

    ImGui::TextDisabled("Overload types (comma-separated, empty = first overload):");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##overload", overload_buf_, sizeof(overload_buf_));

    ImGui::Separator();

    if (install_in_flight_) {
        ImGui::TextDisabled("Installing…");
    } else {
        bool can_install = (class_buf_[0] != '\0' && method_buf_[0] != '\0');
        if (!can_install) ImGui::BeginDisabled();
        if (ImGui::Button("Install", ImVec2(140.0f, 0.0f))) {
            install_hook();
        }
        if (!can_install) ImGui::EndDisabled();
    }

    if (!install_error_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                           install_error_.c_str());
    }
}

void HookEditorTab::draw_log() {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 160.0f);
    ImGui::InputTextWithHint("##logfilter", "Filter log…", log_filter_buf_,
                             sizeof(log_filter_buf_));
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &autoscroll_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lk(log_mu_);
        log_.clear();
    }

    ImGui::Separator();

    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), ImGuiChildFlags_Border);

    std::deque<LogEntry> snapshot;
    {
        std::lock_guard<std::mutex> lk(log_mu_);
        snapshot = log_;
    }

    std::string filter = log_filter_buf_;
    for (const auto& e : snapshot) {
        if (!filter.empty() && e.text.find(filter) == std::string::npos) continue;
        ImVec4 color = e.enter
            ? ImVec4(0.94f, 0.78f, 0.30f, 1.0f)
            : ImVec4(0.65f, 0.85f, 1.0f, 1.0f);
        ImGui::TextColored(color, "%s", e.text.c_str());
    }

    if (autoscroll_ && !snapshot.empty() &&
        ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

} // namespace yamgg
