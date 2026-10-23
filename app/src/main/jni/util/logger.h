// util/logger.h — log-to-UI bridge.
//
// All LOGI/LOGW/LOGE macros in the project log to logcat AND (if a sink
// is registered) forward to a UI subscriber. The UI registers a callback
// once from SettingsTab so a log tab can display recent messages.
//
// There is no buffer size limit. If a sink is registered, every message
// is forwarded synchronously on the logging thread (which is fine
// because logging is rare enough not to impact performance). Callers
// that emit from the JS thread must marshal to the UI thread themselves;
// Logger does not force a marshalling layer because that would add
// cost to every log line.
#ifndef YAMGG_UTIL_LOGGER_H
#define YAMGG_UTIL_LOGGER_H

#include <functional>
#include <string>

namespace yamgg {
namespace util {

enum class LogLevel { Debug, Info, Warn, Error };

using LogSink = std::function<void(LogLevel level, const std::string& tag,
                                    const std::string& msg)>;

// Register the UI sink. Passing an empty function clears it. Thread-safe.
void set_log_sink(LogSink sink);

// Emit a message. Always forwards to logcat; forwards to the sink if
// registered.
void log(LogLevel level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Convenience wrappers.
inline void logd(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
inline void logi(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
inline void logw(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
inline void loge(const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));

} // namespace util
} // namespace yamgg

#endif // YAMGG_UTIL_LOGGER_H
