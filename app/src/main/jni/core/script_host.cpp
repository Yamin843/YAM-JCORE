// core/script_host.cpp
#include "core/script_host.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {

ScriptHost& ScriptHost::instance() {
    static ScriptHost s;
    return s;
}

bool ScriptHost::is_live() const {
    return yamgg::is_ready();
}

void ScriptHost::post_command(uint64_t id, const std::string& op,
                              const std::string& args_json) {
    YamScript* script = bootstrap_script();
    if (script == nullptr) {
        LOGW("post_command(%llu, %s): no bootstrap script",
             static_cast<unsigned long long>(id), op.c_str());
        return;
    }

    // Build the envelope with explicit escaping for op. We trust the
    // caller for args_json (they build it with nlohmann::json::dump).
    std::string envelope;
    envelope.reserve(128 + args_json.size() + op.size());

    envelope.append(R"({"type":"cmd","id":)");
    envelope.append(std::to_string(id));
    envelope.append(R"(,"op":")");
    envelope.append(op);       // op is a fixed identifier, no user data
    envelope.append(R"(","args":)");
    envelope.append(args_json);
    envelope.append("}");

    yam_script_post(script, envelope.c_str(), nullptr);
}

void ScriptHost::post_raw(const std::string& json) {
    YamScript* script = bootstrap_script();
    if (script == nullptr) return;
    yam_script_post(script, json.c_str(), nullptr);
}

} // namespace yamgg
