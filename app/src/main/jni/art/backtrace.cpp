// art/backtrace.cpp
#include "art/backtrace.h"
#include "art/art_guard.h"
#include "core/message_router.h"

#include <android/log.h>

#define LOG_TAG "YAMGG"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace yamgg {
namespace art {

Backtrace& Backtrace::instance() {
    static Backtrace b;
    return b;
}

namespace {

bool parse_result(const nlohmann::json& result,
                  BacktraceResult& out,
                  std::string& error) {
    if (!result.contains("frames") || !result["frames"].is_array()) {
        error = "malformed backtrace reply (no frames array)";
        return false;
    }
    out.frames.clear();
    for (const auto& f : result["frames"]) {
        Frame fr;
        if (f.contains("error")) {
            error = f.value("error", std::string("unknown backtrace error"));
            return false;
        }
        fr.signature  = f.value("signature", std::string());
        fr.className  = f.value("className", std::string());
        fr.methodName = f.value("methodName", std::string());
        fr.origin     = f.value("origin", std::string());
        fr.fileName   = f.value("fileName", std::string());
        fr.lineNumber = f.value("lineNumber", -1);
        if (f.contains("methodFlags") && f["methodFlags"].is_number_unsigned()) {
            fr.methodFlags = f["methodFlags"].get<uint32_t>();
        }
        out.frames.push_back(std::move(fr));
    }
    return true;
}

} // namespace

bool Backtrace::capture(BacktraceResult& out,
                        std::string& error,
                        int limit,
                        int timeout_ms) {
    if (!ArtGuard::instance().is_bridge_available()) {
        error = "Java bridge unavailable";
        return false;
    }

    nlohmann::json args = {{"limit", limit}};
    nlohmann::json result;
    if (!MessageRouter::instance().send_command("backtrace.capture",
                                                args, result, error, timeout_ms)) {
        return false;
    }
    return parse_result(result, out, error);
}

bool Backtrace::capture_thread(uint32_t threadId,
                               BacktraceResult& out,
                               std::string& error,
                               int limit,
                               int timeout_ms) {
    (void)threadId;
    (void)out;
    (void)limit;
    (void)timeout_ms;
    error = "backtrace of arbitrary thread requires ArtStackVisitor; "
            "call capture() for the JS thread, or use Thread.backtrace "
            "via a JS command to get the native stack of a specific thread";
    return false;
}

} // namespace art
} // namespace yamgg
