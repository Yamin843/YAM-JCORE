// ui/tabs/call_panel_tab.h — invoke Java methods, create instances,
// read and write fields, and manage retained handles.
//
// Three sections:
//   Target: class, method/constructor, is_static, this_handle.
//   Args: dynamically rendered per the argument types.
//   Result + handles: shows the returned value and manages retained
//     handles (release one, release all, copy pointer).
#ifndef YAMGG_UI_TABS_CALL_PANEL_TAB_H
#define YAMGG_UI_TABS_CALL_PANEL_TAB_H

#include <chrono>
#include <string>
#include <vector>

#include <json.hpp>

#include "ui/tab.h"

namespace yamgg {

class CallPanelTab : public Tab {
public:
    static CallPanelTab& instance();

    const char* name() const override { return "CL"; }

    void draw() override;

    // Called by the class browser when the user clicks a Call button.
    void prepare_call(const std::string& class_name,
                      const std::string& method_name,
                      const std::vector<std::string>& argument_types,
                      bool is_static);

private:
    CallPanelTab() = default;

    void invoke();
    void release_handle(const std::string& handle);
    void release_all_handles();

    void draw_target_section();
    void draw_args_section();
    void draw_result_section();
    void draw_handles_section();

    void check_timeout();

    // Target state.
    std::string                class_name_;
    std::string                method_name_;
    std::vector<std::string>   argument_types_;
    bool                       is_static_   = false;
    bool                       is_ctor_     = false;
    char                       class_buf_[256]   = {0};
    char                       method_buf_[256]  = {0};
    char                       this_handle_buf_[64] = {0};

    // Argument text buffers, one per argument type.
    std::vector<std::string>   arg_text_;

    // Last result.
    bool                       has_result_   = false;
    bool                       result_ok_    = false;
    nlohmann::json             result_value_;
    std::string                result_error_;
    std::string                result_type_;

    // Handle from last create_instance / instance call.
    std::string                last_handle_;

    // In-flight state.
    bool                       in_flight_    = false;
    std::chrono::steady_clock::time_point started_;
    int                        timeout_seconds_ = 15;

    // Retained handles list (cached from bridge::HandleRegistry).
    struct HandleEntry {
        std::string handle;
        std::string className;
    };
    std::vector<HandleEntry>   handles_cache_;
    char                       handle_filter_buf_[128] = {0};
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_CALL_PANEL_TAB_H
