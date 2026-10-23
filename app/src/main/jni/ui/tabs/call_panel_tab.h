// ui/tabs/call_panel_tab.h — invoke Java methods, create instances,
// read/write fields, and manage retained handles.
//
// When [call] is pressed on a method in the class browser, this tab
// receives the class/method/args and renders an argument editor. Each
// complex argument gets a [create…] button that opens the smart
// ObjectBuilderDialog.
#ifndef YAMGG_UI_TABS_CALL_PANEL_TAB_H
#define YAMGG_UI_TABS_CALL_PANEL_TAB_H

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include <json.hpp>

#include "ui/tab.h"

namespace yamgg {

class CallPanelTab : public Tab {
public:
    using InstanceCreatedCallback =
        std::function<void(const std::string& className,
                           const std::string& handle)>;

    static CallPanelTab& instance();

    const char* name() const override { return "CL"; }

    void draw() override;

    // Called by the class browser (and by the "copy handle" button in
    // other tabs) to prepare the panel for a specific method.
    void prepare_call(const std::string& class_name,
                      const std::string& method_name,
                      const std::vector<std::string>& argument_types,
                      bool is_static,
                      const std::string& this_handle = std::string());

    void set_instance_created_callback(InstanceCreatedCallback cb);

    // Exposed to other tabs so they can prefill the this_handle buffer.
    char* this_handle_buf_for_ui();

private:
    CallPanelTab() = default;

    void invoke();
    void release_handle(const std::string& handle);
    void release_all_handles();
    void check_timeout();

    void draw_target_section();
    void draw_args_section();
    void draw_result_section();
    void draw_handles_section();

    // ── Target ──
    std::string                class_name_;
    std::string                method_name_;
    std::vector<std::string>   argument_types_;
    bool                       is_static_ = false;
    bool                       is_ctor_   = false;
    char                       class_buf_[256]  = {0};
    char                       method_buf_[256] = {0};
    char                       this_handle_buf_[64] = {0};

    // ── Arguments ──
    std::vector<std::string>   arg_text_;
    std::vector<std::string>   arg_handles_;

    // ── Result ──
    bool                       has_result_   = false;
    bool                       result_ok_    = false;
    nlohmann::json             result_value_;
    std::string                result_error_;

    // ── Handles ──
    std::string                last_handle_;
    struct HandleEntry { std::string handle; std::string className; };
    std::vector<HandleEntry>   handles_cache_;
    char                       handle_filter_buf_[128] = {0};

    // ── Operation ──
    bool                       in_flight_ = false;
    std::chrono::steady_clock::time_point started_;
    int                        timeout_seconds_ = 20;

    InstanceCreatedCallback    instance_created_cb_;
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_CALL_PANEL_TAB_H
