// ui/tabs/settings_tab.h — session status, capabilities, and global
// controls.
//
// Read-only information is refreshed automatically once per second.
// The few actions that exist (release all handles, reload font, toggle
// fullscreen) are immediate and idempotent.
#ifndef YAMGG_UI_TABS_SETTINGS_TAB_H
#define YAMGG_UI_TABS_SETTINGS_TAB_H

#include <string>
#include <vector>

#include "ui/tab.h"

namespace yamgg {

class SettingsTab : public Tab {
public:
    static SettingsTab& instance();

    const char* name() const override { return "ST"; }

    void draw() override;

private:
    SettingsTab() = default;

    void draw_session_section();
    void draw_capabilities_section();
    void draw_deoptimize_section();
    void draw_vm_info_section();
    void draw_classloaders_section();
    void draw_open_class_file_section();
    void draw_handles_section();
    void draw_ui_section();

    // ── VM info state ──────────────────────────────────────
    bool        vm_info_requested_ = false;
    std::string vm_info_text_;
    std::string vm_info_error_;

    // ── Class loader list ─────────────────────────────────
    struct LoaderEntry { std::string handle; std::string description; };
    std::vector<LoaderEntry> loaders_cache_;
    bool                     loaders_loading_ = false;
    std::string              loaders_error_;

    // ── openClassFile ─────────────────────────────────────
    char        open_dex_path_buf_[512] = "";
    std::string open_dex_result_;
    bool        open_dex_in_flight_ = false;
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_SETTINGS_TAB_H
