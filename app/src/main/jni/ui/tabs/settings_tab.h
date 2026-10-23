// ui/tabs/settings_tab.h — session status, capabilities, and global
// controls.
//
// Read-only information is refreshed automatically once per second.
// The few actions that exist (release all handles, reload font, toggle
// fullscreen) are immediate and idempotent.
#ifndef YAMGG_UI_TABS_SETTINGS_TAB_H
#define YAMGG_UI_TABS_SETTINGS_TAB_H

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
    void draw_handles_section();
    void draw_ui_section();
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_SETTINGS_TAB_H
