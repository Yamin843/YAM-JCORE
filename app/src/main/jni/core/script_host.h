// core/script_host.h — thin wrapper around the two scripts we load.
//
// bootstrap.cpp owns the lifecycle (create, load, unload, quit). This
// class exists for the rest of the project to post commands to JS without
// having to know which script is active or how GMainContext is threaded.
//
// All commands are asynchronous: ScriptHost::post_command returns
// immediately after yam_script_post schedules the message on the JS
// thread. The reply arrives later through MessageRouter and is
// matched by command id.
#ifndef YAMGG_CORE_SCRIPT_HOST_H
#define YAMGG_CORE_SCRIPT_HOST_H

#include <cstdint>
#include <string>

#include "core/bootstrap.h"

namespace yamgg {

class ScriptHost {
public:
    static ScriptHost& instance();

    // Post a command envelope to the bootstrap script. Caller owns
    // the id used to match the eventual reply. Thread-safe.
    //
    // The envelope format is:
    //   {"type":"cmd","id":<id>,"op":"<op>","args":{...}}
    //
    // JSON is passed as a complete string to avoid pulling the
    // nlohmann::json header into every call site; callers that have
    // structured data build it with nlohmann and call dump().
    void post_command(uint64_t id, const std::string& op,
                      const std::string& args_json);

    // Post a raw JSON envelope verbatim. Used by MessageRouter for
    // protocol-level messages (cancellation, keep-alive, …).
    void post_raw(const std::string& json);

    // True once bootstrap_start has completed successfully.
    bool is_live() const;

private:
    ScriptHost() = default;
};

} // namespace yamgg

#endif // YAMGG_CORE_SCRIPT_HOST_H
