// ui/widgets/input_dialog.cpp
#include "ui/widgets/input_dialog.h"

#include <cstdio>
#include <cstring>

#include "bridge/type_converter.h"
#include "imgui.h"

namespace yamgg {

int InputDialog::text_resize_callback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string* str = static_cast<std::string*>(data->UserData);
        str->resize((size_t)data->BufTextLen);
        data->Buf = const_cast<char*>(str->c_str());
    }
    return 0;
}

InputDialog& InputDialog::instance() {
    static InputDialog d;
    return d;
}

bool InputDialog::is_open() const {
    return open_;
}

void InputDialog::apply_kind_from_type(const std::string& type_name) {
    using bridge::TypeConverter;
    bridge::JavaKind jk = TypeConverter::classify(type_name);
    const char* editor = TypeConverter::editor_kind(jk);

    if (std::strcmp(editor, "void") == 0)         kind_ = Kind::Void;
    else if (std::strcmp(editor, "primitive") == 0) {
        // Distinguish boolean from numeric.
        if (jk == bridge::JavaKind::Boolean || jk == bridge::JavaKind::BoxedBoolean) {
            kind_ = Kind::Boolean;
        } else if (jk == bridge::JavaKind::Char || jk == bridge::JavaKind::BoxedChar) {
            kind_ = Kind::Char;
        } else {
            kind_ = Kind::Primitive;
        }
    }
    else if (std::strcmp(editor, "string") == 0)  kind_ = Kind::Text;
    else if (std::strcmp(editor, "bytes") == 0)   kind_ = Kind::Bytes;
    else if (std::strcmp(editor, "array") == 0)   kind_ = Kind::Json;
    else if (std::strcmp(editor, "map") == 0)     kind_ = Kind::Json;
    else                                          kind_ = Kind::Object;
}

void InputDialog::reset_buffers(const nlohmann::json& initial) {
    text_storage_.clear();
    bytes_storage_.clear();
    json_storage_.clear();
    error_storage_.clear();
    bool_value_ = false;

    // Ensure each storage holds a null terminator so ImGui's c_str()
    // pointer is valid even when the initial value is empty.
    text_storage_.push_back('\0');
    bytes_storage_.push_back('\0');
    json_storage_.push_back('\0');

    if (initial.is_null() || initial.is_discarded()) return;

    switch (kind_) {
        case Kind::Boolean:
            bool_value_ = initial.is_boolean() ? initial.get<bool>() : false;
            break;

        case Kind::Primitive:
        case Kind::Char:
        case Kind::Text:
        case Kind::Object: {
            std::string s;
            if (initial.is_string()) s = initial.get<std::string>();
            else s = initial.dump();
            text_storage_.assign(s.begin(), s.end());
            text_storage_.push_back('\0');
            break;
        }

        case Kind::Bytes: {
            std::string s;
            if (initial.is_object() && initial.contains("hex") &&
                initial["hex"].is_string()) {
                s = initial["hex"].get<std::string>();
            } else if (initial.is_string()) {
                s = initial.get<std::string>();
            } else if (initial.is_array()) {
                for (const auto& v : initial) {
                    int b = v.is_number_integer() ? v.get<int>() & 0xFF : 0;
                    char buf[4];
                    std::snprintf(buf, sizeof(buf), "%02x", b);
                    s += buf;
                }
            }
            bytes_storage_.assign(s.begin(), s.end());
            bytes_storage_.push_back('\0');
            break;
        }

        case Kind::Json:
        case Kind::Void:
        default: {
            std::string s;
            if (initial.is_string()) s = initial.get<std::string>();
            else s = initial.dump(2);
            json_storage_.assign(s.begin(), s.end());
            json_storage_.push_back('\0');
            break;
        }
    }
}

void InputDialog::open(const std::string& title,
                       const std::string& type_name,
                       const nlohmann::json& initial,
                       Callback cb) {
    title_     = title;
    type_name_ = type_name;
    cb_        = std::move(cb);
    open_      = true;

    apply_kind_from_type(type_name_);
    reset_buffers(initial);
}

void InputDialog::cancel() {
    open_ = false;
    cb_  = nullptr;
}

void InputDialog::draw() {
    if (!open_) return;

    char window_title[512];
    std::snprintf(window_title, sizeof(window_title), "%s###input_dialog",
                  title_.c_str());

    ImGui::OpenPopup(window_title);

    ImVec2 viewport = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(viewport.x * 0.55f, 0.0f),
                             ImGuiCond_Appearing);

    bool still_open = true;
    if (ImGui::BeginPopupModal(window_title, &still_open,
            ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::TextDisabled("Type: %s", type_name_.c_str());
        ImGui::Separator();

        auto input_text_dyn = [](const char* id, std::string& storage,
                                  ImGuiInputTextFlags flags = 0) -> bool {
            // Ensure null terminator.
            if (storage.empty() || storage.back() != '\0') storage.push_back('\0');
            return ImGui::InputText(id,
                const_cast<char*>(storage.c_str()),
                storage.capacity() + 1,
                flags | ImGuiInputTextFlags_CallbackResize,
                &InputDialog::text_resize_callback,
                &storage);
        };

        auto input_text_multiline_dyn = [](const char* id, std::string& storage,
                                            const ImVec2& size,
                                            ImGuiInputTextFlags flags = 0) -> bool {
            if (storage.empty() || storage.back() != '\0') storage.push_back('\0');
            return ImGui::InputTextMultiline(id,
                const_cast<char*>(storage.c_str()),
                storage.capacity() + 1,
                size,
                flags | ImGuiInputTextFlags_CallbackResize,
                &InputDialog::text_resize_callback,
                &storage);
        };

        switch (kind_) {
            case Kind::Primitive:
                input_text_dyn("##prim", text_storage_,
                    ImGuiInputTextFlags_CharsDecimal);
                break;

            case Kind::Boolean:
                ImGui::Checkbox("##bool", &bool_value_);
                ImGui::SameLine();
                ImGui::Text("%s", bool_value_ ? "true" : "false");
                break;

            case Kind::Char:
                input_text_dyn("##char", text_storage_,
                    ImGuiInputTextFlags_CharsNoBlank);
                break;

            case Kind::Text:
                input_text_dyn("##text", text_storage_);
                break;

            case Kind::Object:
                input_text_dyn("##object", text_storage_,
                    ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine();
                ImGui::TextDisabled("(handle pointer or null)");
                break;

            case Kind::Bytes:
                ImGui::TextDisabled("Hex bytes (spaces/colons allowed):");
                input_text_multiline_dyn("##bytes", bytes_storage_,
                    ImVec2(0.0f, 120.0f));
                break;

            case Kind::Json:
            case Kind::Void:
            default:
                ImGui::TextDisabled("JSON value:");
                input_text_multiline_dyn("##json", json_storage_,
                    ImVec2(0.0f, 160.0f));
                break;
        }

        if (!error_storage_.empty() && error_storage_[0] != '\0') {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               error_storage_.c_str());
        }

        ImGui::Separator();

        const float button_width = 110.0f;

        if (ImGui::Button("Accept", ImVec2(button_width, 0))) {
            nlohmann::json out;
            std::string err;
            if (validate_and_encode(out, err)) {
                auto cb = std::move(cb_);
                open_ = false;
                cb_ = nullptr;
                ImGui::CloseCurrentPopup();
                if (cb) cb(true, out);
            } else {
                error_storage_.assign(err.begin(), err.end());
                error_storage_.push_back('\0');
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            auto cb = std::move(cb_);
            open_ = false;
            cb_ = nullptr;
            ImGui::CloseCurrentPopup();
            if (cb) cb(false, nlohmann::json());
        }

        ImGui::EndPopup();
    }

    if (!still_open && open_) {
        // User clicked the window's X button.
        auto cb = std::move(cb_);
        open_ = false;
        cb_ = nullptr;
        if (cb) cb(false, nlohmann::json());
    }
}

bool InputDialog::validate_and_encode(nlohmann::json& out, std::string& error) {
    using bridge::TypeConverter;

    if (kind_ == Kind::Void) {
        out = nlohmann::json();
        return true;
    }
    if (kind_ == Kind::Boolean) {
        out = bool_value_;
        return true;
    }

    std::string text;
    auto strip_null = [](std::string s) {
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    };
    if (kind_ == Kind::Bytes)       text = strip_null(bytes_storage_);
    else if (kind_ == Kind::Json)   text = strip_null(json_storage_);
    else                            text = strip_null(text_storage_);

    // Empty string: null for nullable types, error for primitives.
    if (text.empty() && kind_ != Kind::Text) {
        bool nullable = false;
        TypeConverter::classify_with_nullability(type_name_, nullable);
        if (nullable) {
            out = nlohmann::json();
            return true;
        }
        error = "value required";
        return false;
    }

    try {
        out = TypeConverter::encode_user_input(type_name_, text, error);
        if (!error.empty()) return false;
        // Final validation.
        bridge::JavaKind jk = TypeConverter::classify(type_name_);
        if (!TypeConverter::validate(out, jk, error)) return false;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

} // namespace yamgg
