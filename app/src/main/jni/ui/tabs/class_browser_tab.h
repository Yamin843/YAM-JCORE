// ui/tabs/class_browser_tab.h — browse and search Java classes.
//
// Left side: tree of packages and classes (from bridge.class.list).
// Right side: selected class's methods, fields, constructors
// (from bridge.class.inspect), with per-method Hook and Call buttons
// that hand off to the hook_editor and call_panel tabs.
//
// All loading is asynchronous. The UI shows a spinner while a request
// is in flight and a client-side 15s timeout unblocks the spinner if
// the JS thread never replies.
#ifndef YAMGG_UI_TABS_CLASS_BROWSER_TAB_H
#define YAMGG_UI_TABS_CLASS_BROWSER_TAB_H

#include <chrono>
#include <string>
#include <vector>

#include "bridge/class_browser.h"
#include "ui/tab.h"
#include "ui/widgets/tree_view.h"

namespace yamgg {

class ClassBrowserTab : public Tab {
public:
    static ClassBrowserTab& instance();

    const char* name() const override { return "CB"; }

    void draw() override;
    void on_activated() override;

    // Called by other tabs (e.g. hook_editor after a successful hook)
    // to display a class.
    void show_class(const std::string& class_name);

private:
    ClassBrowserTab() = default;

    // ── Instance search (heap.choose) ───────────────────────
    struct InstanceSearchState {
        bool                          running = false;
        uint64_t                      session_id = 0;
        int                           strategy = 0;   // 0=Auto, 1=JVMTI, 2=GetInstances
        int                           total_matches = 0;
        std::chrono::steady_clock::time_point started;
        std::string                   error;
        struct Match {
            std::string handle;
            std::string display;
        };
        std::vector<Match>            matches;
    };
    InstanceSearchState instance_search_;

    void start_instance_search();
    void stop_instance_search();

    // ── Advanced method search (enumerateMethods) ───────────
    struct MethodSearchState {
        bool                          running = false;
        std::string                   pattern;
        std::chrono::steady_clock::time_point started;
        std::string                   error;
        struct Match {
            std::string className;
            std::string methodName;
            std::string loader;
        };
        std::vector<Match>            matches;
    };
    MethodSearchState method_search_;
    char              method_pattern_buf_[256] = "com.example.*!*";

    void start_method_search();

    void draw_instance_search_section();
    void draw_method_search_section();

    struct ListState {
        bool        loading = false;
        std::string error;
        std::chrono::steady_clock::time_point started;
    };

    struct DetailState {
        bool                          loading = false;
        std::string                   class_name;
        bridge::ClassInspection       inspection;
        std::string                   error;
        std::chrono::steady_clock::time_point started;
    };

    void refresh_list();
    void load_detail(const std::string& class_name);
    void check_timeouts();

    void draw_details();

    ListState    list_;
    DetailState  detail_;
    TreeView     tree_;

    char         filter_buf_[256] = {0};
    std::string  last_filter_;
    std::string  selected_class_;

    bool         initialized_ = false;

    // Method selection state for the inline hook dialog trigger.
    int          selected_method_   = -1;
    int          selected_overload_ = -1;
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_CLASS_BROWSER_TAB_H
