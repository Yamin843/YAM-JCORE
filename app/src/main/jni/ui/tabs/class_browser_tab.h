// ui/tabs/class_browser_tab.h — browse classes, trace methods, call
// methods, create instances.
//
// Layout (right side of the split):
//
//   [Find instances] [strategy]           ← instance acquisition
//   [Instances list]                      ← select one for instance calls
//   [Constructors]  $new(...)  [create]   ← create new instance
//   [Methods]       ret name(...)  [trace] [call]  (calls: N)
//   [Fields]        type name
//
// The trace button is a toggle: first click installs a hook, second
// click uninstalls it. The row's text color changes while a hook is
// active, and a live call counter appears next to the buttons.
#ifndef YAMGG_UI_TABS_CLASS_BROWSER_TAB_H
#define YAMGG_UI_TABS_CLASS_BROWSER_TAB_H

#include <chrono>
#include <cstdint>
#include <map>
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

    // Called by other tabs to display a class.
    void show_class(const std::string& class_name);

private:
    ClassBrowserTab() = default;

    // ── Loading state ───────────────────────────────────────
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

    // ── Trace state per method ──────────────────────────────
    struct MethodHookState {
        uint64_t hook_id    = 0;
        bool     active     = false;
    };

    // ── Instance list ───────────────────────────────────────
    struct InstanceItem {
        std::string handle;
        std::string display;
    };

    // ── Method search (global) ──────────────────────────────
    struct MethodSearchState {
        bool                          running = false;
        std::string                   pattern;
        std::chrono::steady_clock::time_point started;
        std::string                   error;
        struct Match {
            std::string className;
            std::string methodName;
        };
        std::vector<Match>            matches;
    };

    // ── Flow control ────────────────────────────────────────
    void refresh_list();
    void load_detail(const std::string& class_name);
    void check_timeouts();

    // ── Drawing ─────────────────────────────────────────────
    void draw_details();
    void draw_instances_section();
    void draw_method_search_bar();

    // ── Actions ─────────────────────────────────────────────
    void toggle_trace(const bridge::MethodSignature& m);
    void call_method(const bridge::MethodSignature& m);
    void create_instance(const bridge::ConstructorSignature& ctor);
    void find_instances();
    void stop_instance_search();

    // ── State ───────────────────────────────────────────────
    ListState    list_;
    DetailState  detail_;
    TreeView     tree_;

    char         filter_buf_[256] = {0};
    std::string  last_filter_;
    std::string  selected_class_;
    bool         initialized_ = false;

    // Per-method hook state. Key is "className|methodName|arg1,arg2,...".
    std::map<std::string, MethodHookState> method_hooks_;

    // Instances of the currently selected class.
    std::vector<InstanceItem>  instances_;
    int                        selected_instance_ = -1;

    // Instance search state.
    bool                       instance_search_running_ = false;
    uint64_t                   instance_search_id_ = 0;
    int                        instance_search_strategy_ = 1;   // 1=JVMTI
    std::string                instance_search_error_;

    // Global method search (top bar).
    MethodSearchState          method_search_;
    char                       method_pattern_buf_[256] = "com.example.*!*";

    void start_method_search();
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_CLASS_BROWSER_TAB_H
