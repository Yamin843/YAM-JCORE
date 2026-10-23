// ui/widgets/object_builder_dialog.h — advanced Java object builder.
//
// Opens for a target class, lists every constructor discovered via
// bridge.class.inspect, and renders a parameter editor for the chosen
// constructor. Parameters that are themselves complex classes get a
// [create] button that recursively opens another builder for that
// type. On success the parent's parameter slot is filled with the
// returned handle.
//
// The dialog is a stack of frames. The top frame is drawn; lower
// frames are suspended until the top completes or is cancelled.
// Only the deepest completion bubbles up through the stack, one level
// at a time.
#ifndef YAMGG_UI_WIDGETS_OBJECT_BUILDER_DIALOG_H
#define YAMGG_UI_WIDGETS_OBJECT_BUILDER_DIALOG_H

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "bridge/class_browser.h"

namespace yamgg {

class ObjectBuilderDialog {
public:
    // Fired when the outermost frame completes. `handle` is the global
    // reference for the created instance.
    using Callback = std::function<void(bool ok,
                                        const std::string& handle,
                                        const std::string& className)>;

    static ObjectBuilderDialog& instance();

    // Open the builder for `className`. If already open, the current
    // stack is discarded before the new one starts.
    void open(const std::string& className, Callback cb);

    // Draw the modal. Safe to call every frame; does nothing when
    // closed.
    void draw();

    bool is_open() const;
    void close();

private:
    ObjectBuilderDialog() = default;

    struct Frame {
        std::string               className;
        bridge::ClassInspection   inspection;
        bool                      loading = false;
        bool                      loaded  = false;
        std::string               error;
        std::chrono::steady_clock::time_point started;

        int                       selected_ctor = 0;

        // One entry per constructor parameter of the selected ctor.
        std::vector<std::string>  arg_types;
        std::vector<std::string>  arg_text;      // typed JSON for simple args
        std::vector<std::string>  arg_handles;   // filled for complex args

        int                       parent_arg_index = -1;  // set when nested
    };

    void load_frame(int frame_index);
    void check_timeouts();
    void try_create();
    void open_nested(int arg_index);

    std::vector<Frame> stack_;
    Callback           cb_;
    bool               open_      = false;
    bool               in_flight_ = false;
    std::string        error_;
    std::chrono::steady_clock::time_point in_flight_start_;
};

} // namespace yamgg

#endif // YAMGG_UI_WIDGETS_OBJECT_BUILDER_DIALOG_H
