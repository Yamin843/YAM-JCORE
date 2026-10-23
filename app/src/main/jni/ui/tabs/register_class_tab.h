// ui/tabs/register_class_tab.h — Java class registration at runtime.
//
// This tab wraps Java.registerClass. Users define a class name, a
// JSON body (superClass, implements, fields, methods), and press
// Register. The tab sends the merged spec to bridge.register.class,
// receives a retained handle to the new class, and displays it.
//
// The JSON body is intentionally left as plain text: building a
// visual editor for every method signature in Java would require a
// full reflection surface in the UI, and the JSON form is both more
// compact and more expressive for the kind of classes this tool
// creates (hooks, proxies, adapters).
#ifndef YAMGG_UI_TABS_REGISTER_CLASS_TAB_H
#define YAMGG_UI_TABS_REGISTER_CLASS_TAB_H

#include <chrono>
#include <string>

#include "ui/tab.h"

namespace yamgg {

class RegisterClassTab : public Tab {
public:
    static RegisterClassTab& instance();

    const char* name() const override { return "RC"; }

    void draw() override;

private:
    RegisterClassTab() = default;

    void register_class();
    void copy_handle_to_clipboard();

    // ── UI state ──────────────────────────────────────────
    char class_name_buf_[256]  = "com.example.YamClass";
    char body_buf_[16384]      =
        "{\n"
        "  \"superClass\": \"java.lang.Object\",\n"
        "  \"implements\": [],\n"
        "  \"fields\": {\n"
        "    \"counter\": { \"type\": \"int\", \"value\": 0 }\n"
        "  },\n"
        "  \"methods\": {\n"
        "    \"run\": {\n"
        "      \"returnType\": \"void\",\n"
        "      \"argumentTypes\": [],\n"
        "      \"implementation\": \"function() { send({type:'log',level:'info',message:'run() invoked'}); }\"\n"
        "    }\n"
        "  }\n"
        "}\n";

    // ── Operation state ───────────────────────────────────
    bool                            in_flight_    = false;
    std::chrono::steady_clock::time_point started_;
    std::string                     result_handle_;
    std::string                     result_message_;
    bool                            result_ok_    = false;
};

} // namespace yamgg

#endif // YAMGG_UI_TABS_REGISTER_CLASS_TAB_H
