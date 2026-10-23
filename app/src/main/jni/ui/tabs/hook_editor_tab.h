// ui/tabs/hook_editor_tab.h — manage Java method hooks.
//
// Three sections:
//   Active hooks: table of installed hooks with an Unhook button each.
//   Installer: pick class+method+overload, then Install.
//   Log: rolling buffer of hook.enter and hook.leave events with filter.
//
// Hook events are pushed by bridge::HookManager::set_global_*_callback.
// They arrive on the JS thread and are marshalled to the UI thread here.
#ifndef YAMGG_UI_TABS_HOOK_EDITOR_TAB_H
#define YAMGG_UI_TABS_HOOK_EDITOR_TAB_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "bridge/hook_manager.h"
#include "ui/tab.h"

namespace yamgg {

class HookEditorTab : public Tab {
public:
    static HookEditorTab& instance();

    const char* name() const override { return "HK"; }

    void draw() override;
    void on_activated() override;

    // Called by other tabs to prefill the installer.
    void prepare_hook(const std::string& class_name,
                      const std::string& method_name,
                      const std::vector<std::string>& overloads);

private:
    HookEditorTab();

    void install_hook();
    void uninstall_hook(uint64_t hook_id);
    void refresh_hook_list();
    void on_enter(const bridge::HookEnterEvent& ev);
    void on_leave(const bridge::HookLeaveEvent& ev);
    void append_log(bool enter, const std::string& text);

    void draw_active_hooks();
    void draw_installer();
    void draw_log();

    std::string  class_name_;
    std::string  method_name_;
    std::string  overload_choice_;  // "(int,java.lang.String)" — empty for "any"
    std::vector<std::string> overload_signatures_;

    char         class_buf_[256]  = {0};
    char         method_buf_[256] = {0};
    char         overload_buf_[512] = {0};

    std::vector<bridge::HookRecord> hooks_cache_;
    std::string                     install_error_;
    bool                            install_in_flight_ = false;
    std::chrono::steady_clock::time_point install_started_;

    // Log ring buffer.
    struct LogEntry {
        bool        enter;
        std::string text;
        std::chrono::steady_clock::time_point when;
    };
    std::deque<LogEntry>  log_;
    std::mutex            log_mu_;
    char                  log_filter_buf_[128] = {0};
    bool                  autoscroll_ = true;

    bool                  initialized_ = false;
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_HOOK_EDITOR_TAB_H
