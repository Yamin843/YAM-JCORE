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
#include <functional>
#include <string>
#include <vector>

#include <json.hpp>

#include "ui/tab.h"

namespace yamgg {

class CallPanelTab : public Tab {
public:
    static CallPanelTab& instance();

    using InstanceCreatedCallback =
        std::function<void(const std::string& className,
                           const std::string& handle)>;

    // Register a callback invoked from the UI thread when create_instance
    // succeeds. The class browser uses this to append the new instance
    // to its own list.
    void set_instance_created_callback(InstanceCreatedCallback cb);

    const char* name() const override { return "CL"; }

    void draw() override;

    // Called by the class browser when the user clicks a Call button.
    void prepare_call(const std::string& class_name,
                      const std::string& method_name,
                      const std::vector<std::string>& argument_types,
                      bool is_static,
                      const std::string& this_handle = std::string());

public:
    // Exposes the this_handle buffer for other tabs (e.g. the class
    // browser's instance search) to prefill it with a selected handle.
    // Returns a pointer to the internal buffer with the size the
    // caller expects for ImGui::InputText.
    char* this_handle_buf_for_ui();
    static constexpr size_t this_handle_buf_size() { return 64; }

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

    InstanceCreatedCallback    instance_created_cb_;
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_CALL_PANEL_TAB_H
