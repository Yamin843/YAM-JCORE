// ui/widgets/input_dialog.h — modal dialog for typing a single value.
//
// Modes:
//   Primitive  — a single-line numeric field (byte/short/int/long/float/double)
//   Boolean    — a checkbox
//   Char       — a single-character field
//   Text       — a single-line text field (String, CharSequence, File, …)
//   Bytes      — a multi-line hex field with a live preview
//   Json       — a multi-line JSON field for maps/collections/objects
//   Handle     — a single-line field for a handle pointer (Java object)
//
// The dialog validates the input against the requested type by calling
// bridge::TypeConverter::encode_user_input and shows any error inline.
// On Accept the callback fires on the UI thread with the encoded value.
#ifndef YAMGG_UI_WIDGETS_INPUT_DIALOG_H
#define YAMGG_UI_WIDGETS_INPUT_DIALOG_H

#include <functional>
#include <string>

#include <json.hpp>

namespace yamgg {

class InputDialog {
public:
    // Same as bridge::TypeConverter::editor_kind output.
    enum class Kind {
        Primitive,
        Boolean,
        Char,
        Text,
        Bytes,
        Json,
        Object,
        Void
    };

    using Callback = std::function<void(bool ok, nlohmann::json value)>;

    static InputDialog& instance();

    // Open the dialog. `title` is shown as the window caption.
    // `type_name` is the Java type (e.g. "int", "java.lang.String",
    // "android.os.Bundle"). The dialog picks Kind automatically.
    // `initial` is the JSON-encoded starting value (empty string means
    // no starting value).
    void open(const std::string& title,
              const std::string& type_name,
              const nlohmann::json& initial,
              Callback cb);

    // Draw the dialog if open. Called once per frame from the tab that
    // owns it (or from MainWindow, whichever is drawn first).
    void draw();

    // Close without firing the callback (called on tab switch, shutdown,
    // etc.).
    void cancel();

    bool is_open() const;

private:
    InputDialog() = default;

    void apply_kind_from_type(const std::string& type_name);
    void reset_buffers(const nlohmann::json& initial);
    bool validate_and_encode(nlohmann::json& out, std::string& error);

    bool         open_        = false;
    std::string  title_;
    std::string  type_name_;
    Kind         kind_        = Kind::Text;
    Callback     cb_;

    // Dynamic text storage. std::string with capacity()+1 gives
    // ImGui the buffer it needs; the CallbackResize handles growth
    // automatically, so there is no fixed upper bound.
    std::string  text_storage_;
    std::string  bytes_storage_;
    std::string  json_storage_;
    std::string  error_storage_;

    // Boolean state for the checkbox variant.
    bool         bool_value_ = false;

    // Callback used by ImGui to resize the string backing a text field.
    static int text_resize_callback(ImGuiInputTextCallbackData* data);
};

} // namespace yamgg

#endif // YAMGG_UI_WIDGETS_INPUT_DIALOG_H
