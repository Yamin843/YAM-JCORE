// ui/tabs/register_all.cpp — instantiate every tab and register it.
//
// Registration order determines sidebar order and the indices used by
// ClassBrowserTab and HookEditorTab when they switch to each other:
//   0 = ClassBrowserTab
//   1 = HookEditorTab
//   2 = CallPanelTab
//   3 = SettingsTab
//
// Tabs are singletons and outlive TabRegistry. We wrap them in a
// shared_ptr whose deleter is a no-op — the singleton owns itself.
#include "ui/main_window.h"
#include "ui/tabs/call_panel_tab.h"
#include "ui/tabs/class_browser_tab.h"
#include "ui/tabs/hook_editor_tab.h"
#include "ui/tabs/settings_tab.h"

#include <memory>

namespace yamgg {

namespace {
struct NoOpDeleter {
    void operator()(Tab*) const noexcept {}
};
}

void register_all_default_tabs() {
    MainWindow& win = MainWindow::instance();
    TabRegistry& reg = TabRegistry::instance();

    if (reg.count() != 0) return;

    auto add = [&win](Tab* ptr) {
        std::shared_ptr<Tab> sp(ptr, NoOpDeleter{});
        win.register_tab(std::move(sp));
    };

    add(&ClassBrowserTab::instance());
    add(&HookEditorTab::instance());
    add(&CallPanelTab::instance());
    add(&SettingsTab::instance());
}

} // namespace yamgg
