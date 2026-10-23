// util/base64.cpp
#include "util/base64.h"

namespace yamgg {
namespace util {

namespace {
constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int8_t decode_char(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return (int8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int8_t)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (int8_t)(c - '0' + 52);
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
} // namespace

std::string base64_encode(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) return std::string();

    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = ((uint32_t)data[i] << 16)
                   | ((uint32_t)data[i + 1] << 8)
                   | ((uint32_t)data[i + 2]);
        out.push_back(kEncodeTable[(v >> 18) & 0x3F]);
        out.push_back(kEncodeTable[(v >> 12) & 0x3F]);
        out.push_back(kEncodeTable[(v >> 6) & 0x3F]);
        out.push_back(kEncodeTable[v & 0x3F]);
        i += 3;
    }

    size_t remaining = len - i;
    if (remaining == 1) {
        uint32_t v = (uint32_t)data[i] << 16;
        out.push_back(kEncodeTable[(v >> 18) & 0x3F]);
        out.push_back(kEncodeTable[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out.push_back(kEncodeTable[(v >> 18) & 0x3F]);
        out.push_back(kEncodeTable[(v >> 12) & 0x3F]);
        out.push_back(kEncodeTable[(v >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

bool base64_decode(const std::string& in,
                   std::vector<uint8_t>& out,
                   std::string& error) {
    out.clear();
    out.reserve((in.size() / 4 + 1) * 3);

    // Strip whitespace.
    std::string clean;
    clean.reserve(in.size());
    for (char c : in) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        clean.push_back(c);
    }

    if (clean.size() % 4 == 1) {
        error = "invalid base64 length";
        return false;
    }

    // Pad to multiple of 4 with '='.
    while (clean.size() % 4 != 0) clean.push_back('=');

    size_t i = 0;
    while (i + 4 <= clean.size()) {
        int8_t v0 = decode_char((uint8_t)clean[i]);
        int8_t v1 = decode_char((uint8_t)clean[i + 1]);
        if (v0 < 0 || v1 < 0) {
            error = "invalid base64 character";
            return false;
        }

        int8_t v2 = (clean[i + 2] == '=') ? 0 : decode_char((uint8_t)clean[i + 2]);
        int8_t v3 = (clean[i + 3] == '=') ? 0 : decode_char((uint8_t)clean[i + 3]);
        if (v2 < 0 || v3 < 0) {
            error = "invalid base64 character";
            return false;
        }

        uint32_t v = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12)
                   | ((uint32_t)v2 << 6) | (uint32_t)v3;
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        if (clean[i + 2] != '=') out.push_back((uint8_t)((v >> 8) & 0xFF));
        if (clean[i + 3] != '=') out.push_back((uint8_t)(v & 0xFF));

        i += 4;
    }

    return true;
}

} // namespace util
} // namespace yamgg
