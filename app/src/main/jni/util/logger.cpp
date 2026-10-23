// util/logger.cpp
#include "util/logger.h"

#include <android/log.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace yamgg {
namespace util {

namespace {
std::mutex              g_sink_mu;
LogSink                 g_sink;

int to_android_priority(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return ANDROID_LOG_DEBUG;
        case LogLevel::Info:  return ANDROID_LOG_INFO;
        case LogLevel::Warn:  return ANDROID_LOG_WARN;
        case LogLevel::Error: return ANDROID_LOG_ERROR;
    }
    return ANDROID_LOG_INFO;
}
} // namespace

void set_log_sink(LogSink sink) {
    std::lock_guard<std::mutex> lk(g_sink_mu);
    g_sink = std::move(sink);
}

void log(LogLevel level, const char* tag, const char* fmt, ...) {
    if (tag == nullptr) tag = "YAMGG";
    if (fmt == nullptr) fmt = "";

    // Render the message once. Use vasprintf so any length works.
    va_list ap;
    va_start(ap, fmt);
    char* buf = nullptr;
    int n = vasprintf(&buf, fmt, ap);
    va_end(ap);

    std::string msg;
    if (n >= 0 && buf != nullptr) {
        msg.assign(buf, (size_t)n);
        free(buf);
    } else {
        msg = "<format error>";
    }

    __android_log_write(to_android_priority(level), tag, msg.c_str());

    LogSink sink;
    {
        std::lock_guard<std::mutex> lk(g_sink_mu);
        sink = g_sink;
    }
    if (sink) {
        sink(level, tag, msg);
    }
}

} // namespace util
} // namespace yamgg
